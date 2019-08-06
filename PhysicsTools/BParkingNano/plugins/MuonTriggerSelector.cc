// class to produce 2 pat::MuonCollections
// one matched to the Park triggers
// another fitered wrt the Park triggers

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Framework/interface/EDProducer.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/ESHandle.h"

#include "DataFormats/PatCandidates/interface/PackedCandidate.h"
#include "DataFormats/PatCandidates/interface/CompositeCandidate.h"

#include "DataFormats/PatCandidates/interface/Muon.h"

#include "DataFormats/PatCandidates/interface/TriggerObjectStandAlone.h"
#include "FWCore/Common/interface/TriggerNames.h"
#include "DataFormats/Common/interface/TriggerResults.h"
#include "DataFormats/PatCandidates/interface/TriggerObjectStandAlone.h"
#include "DataFormats/PatCandidates/interface/PackedTriggerPrescales.h"
#include "DataFormats/PatCandidates/interface/TriggerPath.h"
#include "DataFormats/PatCandidates/interface/TriggerEvent.h"
#include "DataFormats/PatCandidates/interface/TriggerAlgorithm.h"

#include <TLorentzVector.h>

using namespace std;


constexpr float MuonMass_ = 0.10565837;
constexpr bool debug = false;

class MuonTriggerSelector : public edm::EDProducer {
    
public:
    
    explicit MuonTriggerSelector(const edm::ParameterSet &iConfig);
    
    ~MuonTriggerSelector() override {};
    
    
private:

    virtual void produce(edm::Event&, const edm::EventSetup&);

    edm::EDGetTokenT<std::vector<pat::Muon>> muonSrc_;
    edm::EDGetTokenT<edm::TriggerResults> triggerBits_;
    edm::EDGetTokenT<std::vector<pat::TriggerObjectStandAlone>> triggerObjects_;
    edm::EDGetTokenT<pat::PackedTriggerPrescales> triggerPrescales_;
    edm::EDGetTokenT<reco::VertexCollection> vertexSrc_;

    //for trigger match
    const double maxdR_;

    //for filter wrt trigger
    const double dzTrg_cleaning_;
    const double ptMin_;
    const double absEtaMax_;
};


MuonTriggerSelector::MuonTriggerSelector(const edm::ParameterSet &iConfig):
  muonSrc_( consumes<std::vector<pat::Muon>> ( iConfig.getParameter<edm::InputTag>( "muonCollection" ) ) ),
  triggerBits_(consumes<edm::TriggerResults>(iConfig.getParameter<edm::InputTag>("bits"))),
  triggerObjects_(consumes<std::vector<pat::TriggerObjectStandAlone>>(iConfig.getParameter<edm::InputTag>("objects"))),
  triggerPrescales_(consumes<pat::PackedTriggerPrescales>(iConfig.getParameter<edm::InputTag>("prescales"))),
  vertexSrc_( consumes<reco::VertexCollection> ( iConfig.getParameter<edm::InputTag>( "vertexCollection" ) ) ), 
  maxdR_(iConfig.getParameter<double>("maxdR_matching")),
  dzTrg_cleaning_(iConfig.getParameter<double>("dzForCleaning_wrtTrgMuon")),
  ptMin_(iConfig.getParameter<double>("ptMin")),
  absEtaMax_(iConfig.getParameter<double>("absEtaMax"))
{
    produces<pat::MuonCollection>("trgMatched");
    produces<pat::MuonCollection>("trgFiltered");
}



void MuonTriggerSelector::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
    

    edm::Handle<reco::VertexCollection> vertexHandle;
    iEvent.getByToken(vertexSrc_, vertexHandle);
    const reco::Vertex & PV = vertexHandle->front();

    if(debug) std::cout << " MuonTriggerSelector::produce " << std::endl;

    edm::Handle<edm::TriggerResults> triggerBits;
    iEvent.getByToken(triggerBits_, triggerBits);
    const edm::TriggerNames &names = iEvent.triggerNames(*triggerBits);

    std::vector<pat::TriggerObjectStandAlone> triggeringMuons;

    //taken from https://twiki.cern.ch/twiki/bin/view/CMSPublic/WorkBookMiniAOD2016#Trigger
    edm::Handle<std::vector<pat::TriggerObjectStandAlone>> triggerObjects;
    iEvent.getByToken(triggerObjects_, triggerObjects);
    if(debug) std::cout << "\n TRIGGER OBJECTS " << std::endl;

    for (pat::TriggerObjectStandAlone obj : *triggerObjects) { // note: not "const &" since we want to call unpackPathNames
      obj.unpackFilterLabels(iEvent, *triggerBits);
      obj.unpackPathNames(names);

      bool isTriggerMuon = false;
      for (unsigned h = 0; h < obj.filterIds().size(); ++h)
	if(obj.filterIds()[h] == 83){ 
	  isTriggerMuon = true; 
	  if(debug) std::cout << "\t   Type IDs:   " << 83;  //83 = muon
	  break;
	} 

      if(!isTriggerMuon) continue; 
      for (unsigned h = 0; h < obj.filterLabels().size(); ++h){
	std::string filterName = obj.filterLabels()[h];
	if(filterName.find("hltL3") != std::string::npos  && filterName.find("Park") != std::string::npos){
	  isTriggerMuon = true;
	  if(debug) std::cout << "\t   Filters:   " << filterName; 
	  break;
	}
	else{ isTriggerMuon = false; }
      }

      if(!isTriggerMuon) continue;
      triggeringMuons.push_back(obj);
      if(debug){ std::cout << "\tTrigger object:  pt " << obj.pt() << ", eta " << obj.eta() << ", phi " << obj.phi() << std::endl;
	// Print trigger object collection and type
	std::cout << "\t   Collection: " << obj.collection() << std::endl;
      }
    }//trigger objects

    if(debug){
      std::cout << "\n total n of triggering muons = " << triggeringMuons.size() << std::endl;
      for(auto ij : triggeringMuons){
	std::cout << " >>> components (pt, eta, phi) = " << ij.pt() << " " << ij.eta() << " " << ij.phi() << std::endl;
      }
    }


    std::unique_ptr<pat::MuonCollection> resultMatch( new pat::MuonCollection );
    std::unique_ptr<pat::MuonCollection> resultFilter( new pat::MuonCollection );


    //now check for reco muons matched to triggering muons
    edm::Handle<std::vector<pat::Muon>> muons;
    iEvent.getByToken(muonSrc_, muons);

    for(unsigned int iMuo=0; iMuo<muons->size(); ++iMuo){
      const pat::Muon & muon = (*muons)[iMuo];

      //this is for triggering muon not really need to be configurable
      if(!(muon.isLooseMuon() && muon.isSoftMuon(PV))) continue;

      float dRMuonMatching = -1.;
      int recoMuonMatching_index = -1;
      int trgMuonMatching_index = -1;
      for(unsigned int iTrg=0; iTrg<triggeringMuons.size(); ++iTrg){

	float dR = reco::deltaR(triggeringMuons[iTrg], muon);
	if((dR < dRMuonMatching || dRMuonMatching == -1) && dR < maxdR_){
	  dRMuonMatching = dR;
	  recoMuonMatching_index = iMuo;
	  trgMuonMatching_index = iTrg;
	  if(debug) std::cout << " dR = " << dR 
			      << " reco = " << muon.pt() << " " << muon.eta() << " " << muon.phi() << " " 
			      << " HLT = " << triggeringMuons[iTrg].pt() << " " << triggeringMuons[iTrg].eta() << " " << triggeringMuons[iTrg].phi()
			      << std::endl;
	}
      }

      //save reco muon 
      //since we do not store full muon collection => useless to save original muon index
      if(recoMuonMatching_index != -1){
	pat::Muon recoTriggerMuonCand (muon);
	recoTriggerMuonCand.addUserInt("trgMuonIndex", trgMuonMatching_index);
	resultMatch->push_back(recoTriggerMuonCand);
      }
    }
    

    //muons filtered wrt trigger
    // not enough to save allMuons - triggerMuons
    // need to check dZwrtTrg and pt, eta...
    std::vector<int> alreadySavedM;
    alreadySavedM.resize(muons->size(), 0);

    for(auto muonTrg : *resultMatch) {
      
      int icount = -1;
      for(auto muon : *muons) {
	++icount;
	if(alreadySavedM[icount]) continue;

	if((std::fabs(muon.vz() - muonTrg.vz()) > dzTrg_cleaning_ && dzTrg_cleaning_ != -1) ||
	   muon.pt() < ptMin_ || 
	   std::fabs(muon.eta() > absEtaMax_) ) continue;

	alreadySavedM[icount] = 1;
	resultFilter->emplace_back(muon);
      }
    }


    iEvent.put(std::move(resultMatch), "trgMatched");
    iEvent.put(std::move(resultFilter), "trgFiltered");
}



DEFINE_FWK_MODULE(MuonTriggerSelector);

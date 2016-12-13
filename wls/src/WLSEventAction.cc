#include "WLSEventAction.hh"

#include "G4Event.hh"
#include "G4EventManager.hh"
#include "WLSSteppingAction.hh"
#include "WLSDetectorConstruction.hh"

#include "Randomize.hh"
#include <TMath.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <TF1.h>
#include <TH1D.h>
#include <TH2D.h>
#include <TH3D.h>
#include <TNtuple.h>
#include <TFile.h>

#include <TStyle.h>
#include <TText.h>
#include <TGraph.h>
#include <TMarker.h>
#include <TGaxis.h>
#include <TCanvas.h>
#include <TPaveStats.h>

#include "MakeCrvPhotonArrivals.hh"
#include "MakeCrvSiPMResponses.hh"
#include "MakeCrvWaveforms.hh"
#include "MakeCrvRecoPulses.hh"

#include <stdexcept>

#include "CLHEP/Random/Randomize.h"

WLSEventAction* WLSEventAction::_fgInstance = NULL;

WLSEventAction::WLSEventAction(int mode, int numberOfPhotons, int simType, int minBin, bool verbose) : 
                                                                                         _mode(mode), _numberOfPhotons(numberOfPhotons), 
                                                                                         _simType(simType), _minBin(minBin), 
                                                                                         _verbose(verbose), _storeConstants(false)
{
  if(simType==0 && minBin==0) _storeConstants=true;
  _fgInstance = this;

  if(_mode==0 || _mode==1)
  {
    for(int SiPM=0; SiPM<4; SiPM++)
    {
      std::stringstream s0, s1, title;
      s0<<"Photons_Mode_"<<0<<"__SiPM_"<<SiPM;
      s1<<"Photons_Mode_"<<1<<"__SiPM_"<<SiPM;
      title<<"Fiber: "<<SiPM/2<<",  Side: "<<SiPM%2;
      _histP[0][SiPM] = new TH1D(s0.str().c_str(),title.str().c_str(),1000,0,1000);
      _histP[1][SiPM] = new TH1D(s1.str().c_str(),title.str().c_str(),1000,0,1000);
      _histP[0][SiPM]->GetXaxis()->SetTitle("Photons");
      _histP[0][SiPM]->SetLineColor(1);
      _histP[1][SiPM]->GetXaxis()->SetTitle("Photons");
      _histP[1][SiPM]->SetLineColor(2);
    }

    for(int SiPM=0; SiPM<4; SiPM++)
    {
      std::stringstream s0, s1, title;
      s0<<"ArrivalTimes_Mode_"<<0<<"__SiPM_"<<SiPM;
      s1<<"ArrivalTimes_Mode_"<<1<<"__SiPM_"<<SiPM;
      title<<"Fiber: "<<SiPM/2<<",  Side: "<<SiPM%2;
      _histT[0][SiPM] = new TH1D(s0.str().c_str(),title.str().c_str(),250,0,250);
      _histT[1][SiPM] = new TH1D(s1.str().c_str(),title.str().c_str(),250,0,250);
      _histT[0][SiPM]->GetXaxis()->SetTitle("t [ns]");
      _histT[0][SiPM]->SetLineColor(1);
      _histT[1][SiPM]->GetXaxis()->SetTitle("t [ns]");
      _histT[1][SiPM]->SetLineColor(2);
    }

    for(int SiPM=0; SiPM<4; SiPM++)
    {
      std::stringstream s0, title;
      s0<<"PE_SiPM_"<<SiPM;
      title<<"Fiber: "<<SiPM/2<<",  Side: "<<SiPM%2;
      _histPE[SiPM] = new TH1D(s0.str().c_str(),title.str().c_str(),500,0,500);
      _histPE[SiPM]->GetXaxis()->SetTitle("PEs");
      _histPE[SiPM]->SetLineColor(1);
    }

    _ntuple = new TNtuple("CRVNtuple","CRVNtuple","SiPM:photons:PEs:integral:pulseHeight:TOT:recoPEs");
  }
}

WLSEventAction::~WLSEventAction()
{
  if(_mode==0 || _mode==1)
  {
    for(int SiPM=0; SiPM<4; SiPM++)
    {
      delete _histP[0][SiPM];
      delete _histP[1][SiPM];
      delete _histT[0][SiPM];
      delete _histT[1][SiPM];
      delete _histPE[SiPM];
    }
  
    _ntuple->SaveAs("CRVNtuple.root");
    delete _ntuple;
  }

}

void WLSEventAction::BeginOfEventAction(const G4Event* evt)
{
  std::cout<<"Event # "<<evt->GetEventID()<<std::endl;

  WLSSteppingAction::Instance()->Reset();
}

void WLSEventAction::EndOfEventAction(const G4Event* evt)
{
  //create entry in lookup tables
  if(_mode==-1)
  {
    G4Material *fiber = G4Material::GetMaterial("PMMA",true); //fiber
    G4MaterialPropertiesTable* fiberPropertiesTable = fiber->GetMaterialPropertiesTable();
    G4MaterialPropertyVector *rindexFiber = fiberPropertiesTable->GetProperty("RINDEX");
    double speedOfLightFiber = CLHEP::c_light/(*rindexFiber)[0];  //we assume a constant rindex for all energies

    WLSDetectorConstruction *detector = WLSDetectorConstruction::Instance();

    mu2eCrv::LookupBin bin;

    for(int SiPM=0; SiPM<4; SiPM++)
    {
      bin.arrivalProbability[SiPM]=static_cast<float>(WLSSteppingAction::Instance()->GetArrivalTimes(0,SiPM).size())/static_cast<float>(_generatedPhotons);

      float zSiPM=(SiPM%2==0?-detector->GetScintillatorHalfLength():detector->GetScintillatorHalfLength());
      float straightLineTravelTime=fabs(_startZ-zSiPM)/speedOfLightFiber;
      const std::vector<double> &arrivalTimes = WLSSteppingAction::Instance()->GetArrivalTimes(0,SiPM);
      int histTimeDifference[mu2eCrv::LookupBin::nTimeDelays]={0}; //in ns
      for(size_t i=0; i<arrivalTimes.size(); i++)
      {
        int timeDifference=static_cast<int>(arrivalTimes[i]-straightLineTravelTime+0.5);  //rounded to full ns  //the fiber decay time has been set to 0 for mode==-1
        if(timeDifference<0) timeDifference=0;
        if(timeDifference>mu2eCrv::LookupBin::nTimeDelays-1) timeDifference=mu2eCrv::LookupBin::nTimeDelays-1;
        histTimeDifference[timeDifference]++;
      }
      for(size_t i=0; i<mu2eCrv::LookupBin::nTimeDelays; i++)
      {
        float p=static_cast<float>(histTimeDifference[i])/static_cast<float>(arrivalTimes.size());
        bin.timeDelays[SiPM][i]=static_cast<unsigned short>(mu2eCrv::LookupBin::probabilityScale*p+0.5);
      }

      int histEmissions[mu2eCrv::LookupBin::nFiberEmissions]={0};
      const std::vector<int> &fiberEmissions = WLSSteppingAction::Instance()->GetFiberEmissions(SiPM);
      for(size_t i=0; i<fiberEmissions.size(); i++)
      {
        int nEmissions=fiberEmissions[i];
        if(nEmissions<0) nEmissions=0;
        if(nEmissions>mu2eCrv::LookupBin::nFiberEmissions-1) nEmissions=mu2eCrv::LookupBin::nFiberEmissions-1;
        histEmissions[nEmissions]++;
      }
      for(size_t i=0; i<mu2eCrv::LookupBin::nFiberEmissions; i++)
      {
        float p=static_cast<float>(histEmissions[i])/static_cast<float>(fiberEmissions.size());
        bin.fiberEmissions[SiPM][i]=static_cast<unsigned short>(mu2eCrv::LookupBin::probabilityScale*p+0.5);
      }
    }

    if(_verbose)
    {
      for(int SiPM=0; SiPM<4; SiPM++)
      {
        std::cout<<"SiPM: "<<SiPM<<std::endl;
        std::streamsize origPrecision = std::cout.precision();
        std::cout.precision (10);
        std::cout<<"Probability: "<<bin.arrivalProbability[SiPM]<<std::endl;
        std::cout.precision(origPrecision);
        std::cout<<"Time Difference Probabilities: ";
        for(size_t i=0; i<mu2eCrv::LookupBin::nTimeDelays; i++) std::cout<<i<<"/"<<bin.timeDelays[SiPM][i]<<" ";
        std::cout<<std::endl;
        std::cout<<"Fiber Emissions Probabilities: ";
        for(size_t i=0; i<mu2eCrv::LookupBin::nFiberEmissions; i++) std::cout<<i<<"/"<<bin.fiberEmissions[SiPM][i]<<" ";
        std::cout<<std::endl;
      }
      std::cout<<std::endl<<std::endl;
    }

    std::stringstream filename;
    filename<<"LookupTable_"<<_simType<<"_";
    filename.fill('0');
    filename.width(6);
    filename<<_minBin;
    if(evt->GetEventID()==0) std::remove(filename.str().c_str());

//write some constants to the file before the first bin
    if(_storeConstants)
    {
      _storeConstants=false;
      G4Material *scintillator = G4Material::GetMaterial("Polystyrene",true); //scintillator
      G4MaterialPropertiesTable *scintillatorPropertiesTable = scintillator->GetMaterialPropertiesTable();
      G4MaterialPropertyVector *rindexScintillator = scintillatorPropertiesTable->GetProperty("RINDEX");

      mu2eCrv::LookupConstants LC;
      LC.halfThickness      = detector->GetScintillatorHalfThickness(),
      LC.halfWidth          = detector->GetScintillatorHalfWidth(), 
      LC.halfLength         = detector->GetScintillatorHalfLength(),
      LC.speedOfLightFiber  = speedOfLightFiber,
      LC.rindexScintillator = (*rindexScintillator)[0];
      LC.rindexFiber        = (*rindexFiber)[0];
      double cerenkovEnergyMin = rindexScintillator->GetMinLowEdgeEnergy();
      double cerenkovEnergyMax = rindexScintillator->GetMaxLowEdgeEnergy();
      LC.cerenkovEnergyIntervalScintillator = cerenkovEnergyMax - cerenkovEnergyMin;
      cerenkovEnergyMin = rindexFiber->GetMinLowEdgeEnergy();
      cerenkovEnergyMax = rindexFiber->GetMaxLowEdgeEnergy();
      LC.cerenkovEnergyIntervalFiber = cerenkovEnergyMax - cerenkovEnergyMin;
      LC.ratioFastSlow             = scintillatorPropertiesTable->GetConstProperty("YIELDRATIO");  //will not be used later
      LC.scintillatorDensity       = scintillator->GetDensity();
      LC.scintillatorBirksConstant = scintillator->GetIonisation()->GetBirksConstant();  //will not be used later
      LC.fiberSeparation = detector->GetFiberSeparation(),
      LC.holeRadius      = detector->GetHoleRadius(),
      LC.fiberRadius     = detector->GetClad2Radius();
      LC.Write(filename.str());

      mu2eCrv::LookupBinDefinitions LBD;
      LBD.xBins     = WLSDetectorConstruction::Instance()->GetXBins();
      LBD.yBins     = WLSDetectorConstruction::Instance()->GetYBins();
      LBD.zBins     = WLSDetectorConstruction::Instance()->GetZBins();
      LBD.betaBins  = WLSDetectorConstruction::Instance()->GetBetaBins();
      LBD.thetaBins = WLSDetectorConstruction::Instance()->GetThetaBins();
      LBD.phiBins   = WLSDetectorConstruction::Instance()->GetPhiBins();
      LBD.rBins     = WLSDetectorConstruction::Instance()->GetRBins();
      LBD.Write(filename.str());
    }

//write the data to file
    bin.Write(filename.str());
  }

  //fill histograms if a simulation is run
  if(_mode==0 || _mode==1)
  {
    for(int m=0; m<=_mode; m++)
    {
      for(int SiPM=0; SiPM<4; SiPM++)
      {
        _histP[m][SiPM]->Fill(WLSSteppingAction::Instance()->GetArrivalTimes(m,SiPM).size());
        const std::vector<double> &arrivalTimes = WLSSteppingAction::Instance()->GetArrivalTimes(m,SiPM);
        for(size_t i=0; i<arrivalTimes.size(); i++) _histT[m][SiPM]->Fill(arrivalTimes[i]);
      }
    }

    Draw(evt);

    std::cout<<"Photons: ";
    for(int SiPM=0; SiPM<4; SiPM++) std::cout<<_histP[0][SiPM]->GetMean()<<"/"<<_histP[1][SiPM]->GetMean()<<"  ";
    std::cout<<std::endl;
    std::cout<<"PEs: ";
    for(int SiPM=0; SiPM<4; SiPM++) std::cout<<_histPE[SiPM]->GetMean()<<"       ";
    std::cout<<std::endl;
    std::cout<<"Times: ";
    for(int SiPM=0; SiPM<4; SiPM++) std::cout<<_histT[0][SiPM]->GetMean()<<"/"<<_histT[1][SiPM]->GetMean()<<"  ";
    std::cout<<std::endl;
  }
}

void WLSEventAction::Draw(const G4Event* evt) 
{
  mu2eCrv::MakeCrvSiPMResponses::ProbabilitiesStruct probabilities;
  probabilities._constGeigerProbCoef = 1;
  probabilities._constGeigerProbVoltScale = 5.5;
  probabilities._constTrapType0Prob = 0.0;  
  probabilities._constTrapType1Prob = 0.0;
  probabilities._constTrapType0Lifetime = 5;
  probabilities._constTrapType1Lifetime = 50;
  probabilities._constThermalProb = 2.9e-3; 
  probabilities._constPhotonProduction = 0.136; 

  probabilities._constTrapType0Prob = 0;  
  probabilities._constTrapType1Prob = 0;
  probabilities._constThermalProb = 0; 
  probabilities._constPhotonProduction = 0; 

  static CLHEP::HepJamesRandom engine(1);
  static CLHEP::RandFlat randFlat(engine);
  static CLHEP::RandPoissonQ randPoissonQ(engine);
  mu2eCrv::MakeCrvSiPMResponses sim(randFlat,randPoissonQ);
  sim.SetSiPMConstants(1584, 615, 2.4, 0, 1695, 0.08, probabilities);

  mu2eCrv::MakeCrvWaveforms makeCrvWaveform, makeCrvWaveform2;
  double digitizationInterval = 12.5; //ns
  double digitizationInterval2 = 1.0; //ns
  makeCrvWaveform.LoadSinglePEWaveform("/mu2e/app/users/ehrlich/work_08302015/Offline/CRVResponse/standalone/wls-build/singlePEWaveform.txt", 1.0, 100);
  makeCrvWaveform2.LoadSinglePEWaveform("/mu2e/app/users/ehrlich/work_08302015/Offline/CRVResponse/standalone/wls-build/singlePEWaveform.txt", 1.0, 100);

  mu2eCrv::MakeCrvRecoPulses makeRecoPulses(0.005,0.2, 0.0,162.0);

  double startTime=-G4UniformRand()*digitizationInterval;
  std::vector<double> siPMtimes[4], siPMcharges[4];
  std::vector<double> waveform[4], waveform2[4];
  for(int SiPM=0; SiPM<4; SiPM++)
  {
    const std::vector<double> &photonTimes = WLSSteppingAction::Instance()->GetArrivalTimes(_mode,SiPM);

    std::vector<mu2eCrv::SiPMresponse> SiPMresponseVector;
    sim.Simulate(photonTimes, SiPMresponseVector);
    for(unsigned int i=0; i<SiPMresponseVector.size(); i++)
    {
      siPMtimes[SiPM].push_back(SiPMresponseVector[i]._time);
      siPMcharges[SiPM].push_back(SiPMresponseVector[i]._charge);
    }

    makeCrvWaveform.MakeWaveform(siPMtimes[SiPM], siPMcharges[SiPM], waveform[SiPM], startTime, digitizationInterval);
    makeCrvWaveform2.MakeWaveform(siPMtimes[SiPM], siPMcharges[SiPM], waveform2[SiPM], startTime, digitizationInterval2);
  }

  std::ostringstream s1;
  s1<<"waveform_"<<evt->GetEventID();

  gStyle->SetOptStat(0);
  TCanvas c(s1.str().c_str(),s1.str().c_str(),1000,1000);
  c.Divide(2,2);
  TGraph *graph[4]={NULL};
  TGraph *graph2[4]={NULL};
  TH1D *hist[4], *histSiPMResponse[4];
  std::vector<TGraph*> graphVector;
  std::vector<TMarker*> markerVector;
  std::vector<TGaxis*> axisVector;
  std::vector<TLine*> lineVector;
  std::vector<TText*> textVector;

  for(int SiPM=0; SiPM<4; SiPM++)
  {
    c.cd(SiPM+1);

//Photon Arrival Times
    std::ostringstream s2, s3;
    s2<<"Photons_"<<evt->GetEventID()<<"__"<<SiPM;
    s3<<"Fiber: "<<SiPM/2<<",  Side: "<<SiPM%2;
    hist[SiPM]=new TH1D(s2.str().c_str(),s3.str().c_str(),100,0,300);
    const std::vector<double> &photonTimes = WLSSteppingAction::Instance()->GetArrivalTimes(_mode,SiPM);
    for(unsigned int i=0; i<photonTimes.size(); i++)
    {
      hist[SiPM]->Fill(photonTimes[i]);
    }

    hist[SiPM]->SetLineColor(kBlue);
    hist[SiPM]->GetXaxis()->SetTitle("t [ns]");
    hist[SiPM]->GetYaxis()->SetTitle("Photons");
    hist[SiPM]->GetYaxis()->SetTitleOffset(0.5);
    hist[SiPM]->GetYaxis()->SetAxisColor(kBlue);
    hist[SiPM]->GetYaxis()->SetTitleColor(kBlue);
    hist[SiPM]->GetYaxis()->SetLabelColor(kBlue);
    hist[SiPM]->Draw();

//SiPM response
    double scaleSiPMResponse = 0.5;
    double totalPEs=0;
    histSiPMResponse[SiPM]=new TH1D((s2.str()+"SiPMResponse").c_str(),"",100,0,300);
    for(unsigned int j=0; j<siPMtimes[SiPM].size(); j++)
    {
      histSiPMResponse[SiPM]->Fill(siPMtimes[SiPM][j], siPMcharges[SiPM][j]*scaleSiPMResponse);
      totalPEs+=siPMcharges[SiPM][j];
    }
    histSiPMResponse[SiPM]->SetLineColor(kOrange);
    histSiPMResponse[SiPM]->Draw("same");
    _histPE[SiPM]->Fill(totalPEs);

//waveforms with 1 ns bin width
    unsigned int n2 = waveform2[SiPM].size();
    if(n2==0) continue;
    double *t2 = new double[n2];
    double *v2 = new double[n2];
    double histMax = hist[SiPM]->GetMaximum();
    double waveformMax = *std::max_element(waveform2[SiPM].begin(),waveform2[SiPM].end());
    double scale = histMax/waveformMax;
    for(unsigned int j=0; j<n2; j++)
    {
      t2[j]=startTime+j*digitizationInterval2;
      v2[j]=waveform2[SiPM][j];
      v2[j]*=scale;
    }
    graph2[SiPM]=new TGraph();
    graph2[SiPM]->SetTitle("");
    graph2[SiPM]->SetLineWidth(1);
    graph2[SiPM]->SetLineColor(kRed);
    graph2[SiPM]->DrawGraph(n2,t2,v2,"same");

    delete[] t2;
    delete[] v2;

//waveforms with 12.5 ns bin width
    unsigned int n = waveform[SiPM].size();
    if(n==0) continue;
    double *t = new double[n];
    double *v = new double[n];
    for(unsigned int j=0; j<n; j++)
    {
      t[j]=startTime+j*digitizationInterval;
      v[j]=waveform[SiPM][j];
      v[j]*=scale;
    }
    graph[SiPM]=new TGraph(n,t,v);
    graph[SiPM]->SetTitle("");
    graph[SiPM]->SetMarkerStyle(24);
    graph[SiPM]->SetMarkerSize(1.5);
    graph[SiPM]->SetMarkerColor(kRed);
    graph[SiPM]->Draw("sameP");

    delete[] t;
    delete[] v;

//waveforms with 12.5 ns bin width for points above the threshold
//used for the integral
    for(unsigned int j=0; j<n; j++)
    {
      double tI=startTime+j*digitizationInterval;
      double vI=waveform[SiPM][j];
      if(tI>300) break;
      if(vI<=0.005) continue;  //below threshold for integral
      TMarker *marker = new TMarker(tI, vI*scale, markerVector.size());
      markerVector.push_back(marker);
      marker->SetMarkerStyle(20);
      marker->SetMarkerSize(1.5);
      marker->SetMarkerColor(kRed);
      marker->Draw("same");
    }

//Landau fit
    makeRecoPulses.SetWaveform(waveform[SiPM], startTime, digitizationInterval);
    unsigned int nPulse = makeRecoPulses.GetNPulses();
    for(unsigned int pulse=0; pulse<nPulse; pulse++)
    {
      double tL1=makeRecoPulses.GetT1(pulse);
      double tL2=makeRecoPulses.GetT2(pulse);
      int nL=(tL2-tL1)/1.0 + 1;
      double *tL = new double[nL];
      double *vL = new double[nL];
      for(int iL=0; iL<nL; iL++)
      {
        double p0 = makeRecoPulses.GetLandauParam0(pulse);
        double p1 = makeRecoPulses.GetLandauParam1(pulse);
        double p2 = makeRecoPulses.GetLandauParam2(pulse);
        tL[iL] = tL1 + iL*1.0;
        vL[iL] = p0*TMath::Landau(tL[iL], p1, p2);
        vL[iL]*=scale;
        if(isnan(vL[iL])) nL=0;
      }
      if(nL>0)
      {
        TGraph *graphL=new TGraph();
        graphVector.push_back(graphL);
        graphL->SetTitle("");
        graphL->SetLineWidth(2);
        graphL->SetLineColor(kGreen);
        graphL->DrawGraph(nL,tL,vL,"same");
      }

      double leadingEdge=makeRecoPulses.GetLeadingEdge(pulse);
      if(!isnan(leadingEdge) && leadingEdge<300.0)
      {
        TMarker *marker = new TMarker(leadingEdge,
                                      0.2*makeRecoPulses.GetPulseHeight(pulse)*scale,
                                      markerVector.size());
        markerVector.push_back(marker);
        marker->SetMarkerStyle(21);
        marker->SetMarkerSize(1.5);
        marker->SetMarkerColor(kGreen);
        marker->Draw("same");
      }
      delete[] tL;
      delete[] vL;
    }

    if(makeRecoPulses.GetNPulses()>0) 
    {
      int photons = photonTimes.size();
      double integral = makeRecoPulses.GetIntegral(0);
      double pulseHeight = makeRecoPulses.GetPulseHeight(0);
      double recoPEs = makeRecoPulses.GetPEs(0);
      double PEs=0;
      double leadingEdge=makeRecoPulses.GetLeadingEdge(0);
      double TOT=makeRecoPulses.GetTimeOverThreshold(0);
      for(size_t j=0; j<siPMtimes[SiPM].size(); j++) 
      {
        if(siPMtimes[SiPM][j]<leadingEdge+TOT-30.0 && siPMtimes[SiPM][j]>leadingEdge-40.0) PEs+=siPMcharges[SiPM][j];
      } 

      _ntuple->Fill(SiPM,photons,PEs,integral,pulseHeight,TOT,recoPEs);
      _PEs[SiPM].push_back(PEs);
      double avgPEs=0;
      for(size_t j=0; j<_PEs[SiPM].size(); j++) {avgPEs+=_PEs[SiPM][j];}
      avgPEs/=_PEs[SiPM].size();
      std::cout<<"SiPM: "<<SiPM<<" PEs: "<<PEs<<"   average: "<<avgPEs<<std::endl;
    }
    else 
    {
      std::cout<<"SiPM: "<<SiPM<<" "<<makeRecoPulses.GetNPulses()<<" reco pulses: ";
      for(size_t j=0; j<makeRecoPulses.GetNPulses(); j++) std::cout<<makeRecoPulses.GetLeadingEdge(j)<<" ";
      std::cout<<std::endl;
    }

    TGaxis *axis = new TGaxis(270.0,0,270.0,histMax,0,histMax/scale,10,"+L");
    axisVector.push_back(axis);
    axis->SetTitle("voltage [V]");
    axis->SetTitleOffset(-0.5);
    axis->SetTitleColor(kRed);
    axis->SetLineColor(kRed);
    axis->SetLabelColor(kRed);
    axis->Draw("same");

    TGaxis *axisSiPMResponse = new TGaxis(300.0,0,300.0,histMax,0,histMax/scaleSiPMResponse,10,"+L");
    axisVector.push_back(axisSiPMResponse);
    axisSiPMResponse->SetTitle("SiPM output [PE]");
    axisSiPMResponse->SetTitleOffset(-0.5);
    axisSiPMResponse->SetTitleColor(kOrange);
    axisSiPMResponse->SetLineColor(kOrange);
    axisSiPMResponse->SetLabelColor(kOrange);
    axisSiPMResponse->Draw("same");

    TLine *landauLine = new TLine(100, histMax*0.9, 130, histMax*0.9);
    lineVector.push_back(landauLine);
    landauLine->SetLineWidth(2);
    landauLine->SetLineColor(kGreen);
    landauLine->Draw("same");
    TMarker *landauMarker = new TMarker(110, histMax*0.9, markerVector.size());
    markerVector.push_back(landauMarker);
    landauMarker->SetMarkerStyle(21);
    landauMarker->SetMarkerSize(1.5);
    landauMarker->SetMarkerColor(kGreen);
    landauMarker->Draw("same");
    TText *landauText1 = new TText(100, histMax*0.82, "Landau fit with");
    TText *landauText2 = new TText(100, histMax*0.76, "leading edge");
    textVector.push_back(landauText1);
    textVector.push_back(landauText2);
    landauText1->SetTextSize(0.03);
    landauText2->SetTextSize(0.03);
    landauText1->SetTextColor(kGreen);
    landauText2->SetTextColor(kGreen);
    landauText1->Draw("same");
    landauText2->Draw("same");
  }
  if(evt->GetEventID()<20) c.SaveAs((s1.str()+".C").c_str());

  gStyle->SetOptStat(1111);
  TCanvas c1("Photons","Photons",1000,1000);
  c1.Divide(2,2);
  for(int SiPM=0; SiPM<4; SiPM++)
  {
    c1.cd(SiPM+1);
    gPad->SetLogy();
    _histP[0][SiPM]->Draw();
    gPad->Update();
    TPaveStats *stats0 = (TPaveStats*)_histP[0][SiPM]->FindObject("stats");
    stats0->SetTextColor(1);
    stats0->SetLineColor(1);
    double X1 = stats0->GetX1NDC();
    double Y1 = stats0->GetY1NDC();
    double X2 = stats0->GetX2NDC();
    double Y2 = stats0->GetY2NDC();
    _histP[1][SiPM]->Draw();
    gPad->Update();
    TPaveStats *stats1 = (TPaveStats*)_histP[1][SiPM]->FindObject("stats");
    stats1->SetTextColor(2);
    stats1->SetLineColor(2);
    stats1->SetX1NDC(X1);
    stats1->SetY1NDC(Y1-(Y2-Y1));
    stats1->SetX2NDC(X2);
    stats1->SetY2NDC(Y1);
    _histP[0][SiPM]->Draw();
    _histP[1][SiPM]->Draw("same");
  }      
  c1.SaveAs("Photons.C");

  TCanvas c2("ArrivalTimes","ArrivalTimes",1000,1000);
  c2.Divide(2,2);
  for(int SiPM=0; SiPM<4; SiPM++)
  {
    c2.cd(SiPM+1);
    gPad->SetLogy();
    _histT[0][SiPM]->Draw();
    gPad->Update();
    TPaveStats *stats0 = (TPaveStats*)_histT[0][SiPM]->FindObject("stats");
    stats0->SetTextColor(1);
    stats0->SetLineColor(1);
    double X1 = stats0->GetX1NDC();
    double Y1 = stats0->GetY1NDC();
    double X2 = stats0->GetX2NDC();
    double Y2 = stats0->GetY2NDC();
    _histT[1][SiPM]->Draw();
    gPad->Update();
    TPaveStats *stats1 = (TPaveStats*)_histT[1][SiPM]->FindObject("stats");
    stats1->SetTextColor(2);
    stats1->SetLineColor(2);
    stats1->SetX1NDC(X1);
    stats1->SetY1NDC(Y1-(Y2-Y1));
    stats1->SetX2NDC(X2);
    stats1->SetY2NDC(Y1);
    _histT[0][SiPM]->Draw();
    _histT[1][SiPM]->Draw("same");
  }      
  c2.SaveAs("ArrivalTimes.C");

  TCanvas c3("PEs","PEs",1000,1000);
  c3.Divide(2,2);
  for(int SiPM=0; SiPM<4; SiPM++)
  {
    c3.cd(SiPM+1);
    gPad->SetLogy();
    _histPE[SiPM]->Draw();
  }      
  c3.SaveAs("PEs.C");

  for(int SiPM=0; SiPM<4; SiPM++)
  {
    delete hist[SiPM];
    delete histSiPMResponse[SiPM];
    if(graph[SiPM]) delete graph[SiPM];
    if(graph2[SiPM]) delete graph2[SiPM];
  }
  for(size_t i=0; i<graphVector.size(); i++) delete graphVector[i];
  for(size_t i=0; i<markerVector.size(); i++) delete markerVector[i];
  for(size_t i=0; i<axisVector.size(); i++) delete axisVector[i];
  for(size_t i=0; i<textVector.size(); i++) delete textVector[i];
  for(size_t i=0; i<lineVector.size(); i++) delete lineVector[i];
}


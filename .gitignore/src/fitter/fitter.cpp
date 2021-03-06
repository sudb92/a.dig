#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <TROOT.h>
#include <Math/Functor.h>
#include <TMath.h>
#include <Fit/Fitter.h>
#include "ChannelMap.h"
#include "DataStructs.h"
/*
 * 	chi2 = Sum_(w=0 to Nw, r = 0 to Nr) [ (sw/sr - Swr)/Delta_Swr ]^2
 * 
 * Fix sw = 1.0 for w = 0;
 * chi2 = Sum_(r=0 to Nr) [ (1./sr - S0r)/Delta_S0r]^2  + Sum_(w=1 to Nw, r=0 to Nr) [(sw/sr - Swr)/Delta_Swr]^2
 * Minimize the first part to get estimates on all sr. Then, use these estimates to minimize the remaining sum.
 */
int main(int argc, char** argv)
{

if(argc < 2){
std::cout << " Usage: ./findGainMatchFactors <runNumber> <sabre_detector_number>" << std::endl;
return -1;
}

ChannelMap cMap("./etc/ChannelMap_Feb2021_SABRE.txt");
int runID = std::stoi(argv[1]);
int sabrebd = std::stoi(argv[2]);
std::string filename(Form("./run%d/det%d/outlist.%d",runID,sabrebd,sabrebd));
std::ifstream in(filename);
if(!in.is_open()){
	 std::cout << "Error! Couldn't find file at " << filename << " ... check it exists." << std::endl;
	 return -1;
}


int ring, wedge;
double slope, sloperr;
double slopes[16][8], sloperrs[16][8];
double RingFactors[16] = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1} ;
double WedgeFactors[8] = {1,1,1,1, 1,1,1,1};

//First 16 rings, rest wedges. Fix ring 0 at 1.0 
double paramvector[24] = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
			  1,1,1,1, 1,1,1,1};

std::string str1;
while(std::getline(in,str1,'\n'))
{
   if(str1.find("+/-")!=std::string::npos && str1.find("p0")==std::string::npos)
   {
	std::istringstream bb(str1);
	std::string str2;
	bb>>ring>>wedge>>slope>>str2;
	auto pos1 = str2.find("(");
	auto pos2 = str2.find(",");
	auto pos3 = str2.find(")");
	double up = std::stod(str2.substr(pos1+1,pos2-pos1-1));
	double down = std::stod(str2.substr(pos2+1,pos3-pos2-1));
//	std::cout << ring << " " << wedge << " " << slope << " " << up << " " << down << std::endl;
   	slopes[ring][wedge] = slope;
	if(up>down && up>0) sloperrs[ring][wedge] = up; 
	else if(down>0) sloperrs[ring][wedge] = down;
	else sloperrs[ring][wedge] = 0.001;
	//std::cout << b << " ee" << std::endl;
   }
}
//Select wedge=0 as reference for gain=1.0
   auto chi2Function = [&](const double *param) {
      //minimisation function computing the sum of squares of residuals
      // looping at the graph points


     RingFactors[0] = 1.0;
     for(int i=0; i<23; i++){ //0 to 22 = 23 parameters, 0-14 are rings 1 to 15, 15-22 are wedges 0 to 7
	if(i<15) RingFactors[i+1] = param[i];
	else WedgeFactors[i-15] = param[i];
     }

     double chi2 = 0.0;
     
     for(int i=0; i<16; i++)
	for(int j=0; j<8; j++){
		if((sabrebd==1 && i==13) || (sabrebd==2 && j==1) || (sabrebd==4 && j==4) || (sabrebd==4 && i==1) ) continue; //don't add contributions from bad channels
		chi2 += pow((RingFactors[i]/WedgeFactors[j] - slopes[i][j])/sloperrs[i][j],2);
	}
     return chi2;
   };
 
   // wrap chi2 function in a function object for the fit
   // 23 is the number of fit parameters (size of array par)
   ROOT::Math::Functor fcn(chi2Function,23);
   ROOT::Fit::Fitter  fitter;
  
   double pStart[23] = {1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
			       1,1,1,1, 1,1,1,1};
   fitter.SetFCN(fcn, pStart);
   for(int i=0; i<23; i++)
   if(i<15)  fitter.Config().ParSettings(i).SetName(Form("ring%d",i+1));
   else fitter.Config().ParSettings(i).SetName(Form("wedge%d",i-15));
 
   
 
   // do the fit
   bool ok = fitter.FitFCN();
   if (!ok) {
      Error("Fit Error","Fit failed");
   }
 
   const ROOT::Fit::FitResult & result = fitter.Result();
   result.Print(std::cerr);


   
   double *paramResults = (double *)result.GetParams();

     RingFactors[0] = 1.0;
     for(int i=0; i<23; i++){ //0 to 22 = 23 parameters, 0-14 are rings 1 to 15, 15-22 are wedges 0 to 7
	if(i<15) RingFactors[i+1] = paramResults[i];
	else WedgeFactors[i-15] = paramResults[i];
     }

	//Get global scalefactors
	std::ifstream gainf("./etc/global_gain_scalefactors.dat");

    if(!gainf.is_open()){
     std::cout << "Error! Couldn't find file at " << filename << " ... check it exists." << std::endl;
	 return -1;
    }
	double ggains[5];
	while(!gainf.eof()){
	int bd;
	double gain;
	gainf>>bd>>gain;
	ggains[bd] = gain;
	}
   
   for(int j=0; j<8; j++)
   for(int i=0; i<128; i++)
	{
//		std::cout << i << " " << j << " " << slopes[i][j] << " " << RingFactors[i]/WedgeFactors[j]  << " " << slopes[i][j] - RingFactors[i]/WedgeFactors[j] << std::endl;
		auto chaninfo = cMap.FindChannel(i);		
		if(chaninfo->second.detectorType== SABREWEDGE && 
		   chaninfo->second.detectorPart== j && 
		   chaninfo->second.detectorID== sabrebd)
		{
			std::cout << "Chans " << chaninfo->first << "\t" << WedgeFactors[j]*ggains[sabrebd] << std::endl;
		} 
	}

   for(int j=0; j<16; j++)
   for(int i=0; i<128; i++)
	{
//		std::cout << i << " " << j << " " << slopes[i][j] << " " << RingFactors[i]/WedgeFactors[j]  << " " << slopes[i][j] - RingFactors[i]/WedgeFactors[j] << std::endl;
		auto chaninfo = cMap.FindChannel(i);		
		if(chaninfo->second.detectorType== SABRERING && 
		   chaninfo->second.detectorPart== j && 
		   chaninfo->second.detectorID== sabrebd)
		{
			std::cout << "Chans " << chaninfo->first << "\t" << RingFactors[j]*ggains[sabrebd] << std::endl;
		} 
	}



}

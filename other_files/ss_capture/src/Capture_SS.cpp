#include "capture_SS.h"
#include <stdlib.h> 

Capture_SS::Capture_SS()
{

init();

long second=0;
while (true)	{
	
	int * tmp = get_proc_net_wireless();
	cout << "link ("<<tmp[0]<<") level ("<<tmp[1]<<") noise ("<<tmp[2]<<") "<<second++<<"\n";
	sleep(1);

	}

}

Capture_SS::~Capture_SS()
{

}

void Capture_SS::init()
{
	linkQuality,levelQuality,noise=0;
}

int* Capture_SS::get_proc_net_wireless()
{
	char line[256];
	
	ifstream instream ("/proc/net/wireless");
	if ( !instream)
  {
    cout << "\n";
    cout << "instream read failed\n";
  }
	
///rid of the 2 initial disciption lines
	instream.getline(line,256);
	instream.getline(line,256);

///rid of the 2 placeholder words before the quality vars
	instream >> line;
	instream >> line;

///grab link, level and noise
	instream >> line;
	string tmp(line);
	tmp[2] = 0;
	linkQuality = atoi(tmp.c_str());
	
	instream >> levelQuality;
	
	instream >> line;
	string tmp2 (line);
	tmp2[2] = 0;
	noise = atoi(tmp2.c_str());
	
///close stream
	instream.close();

///return
int out [3] = {linkQuality,levelQuality, noise};
return out;
}
#include <iostream>
#include <fstream>
#include <string>

using namespace std;

class Capture_SS	{
public:
Capture_SS();
~Capture_SS();


int linkQuality,levelQuality,noise;

void init();

int* get_proc_net_wireless();

};

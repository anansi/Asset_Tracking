#include "capture_SS.h"

Capture_SS::Capture_SS()
{

init();
get_proc_net_wireless();

}

Capture_SS::~Capture_SS()
{

}

void Capture_SS::init()
{

}

char** Capture_SS::get_proc_net_wireless()
{
	char * line[256];

	ifstream instream ("/proc/net/wireless");
	line = instream.getLine();
	cout << "hello";
	cout << line << endl;
	return "tmp";
}
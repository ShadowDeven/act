/*************************************************************************
  > File Name: main.cc
  > Author: wsun
  > Mail:sunweiflyus@gmail.com
  > Created Time: Thu 13 Apr 2017 09:46:38 PM CDT
  > Comments:
 ************************************************************************/
#include <iostream>
#include "share.h"
#include "random_uniform.cpp"
#include "granularity.cc"
#include "feedback.cc"
#include "process_file_to_map.cc"
#include "pearson.cc"
#include "string.h"
#include "signal.h"
#include "math.h"

using namespace std;

//all global functional variables
//int TRIES_RANDOM = 0;	
int TOTAL_EXECUTION = 0;	//counter of the num of executions 
int MAIL_MODE = 0;	//switch of email alert function
int TRIES_PER = 1; //define how many times ns3 runs for a group of input parameters
//#define TRIES_PER 1 //For each configuration

//global variables
COVG_MAP_VEC covg_map_vec;	//map coverage vector 
Config_Map config_map;		//typedef map<int, vector<struct Test_Parems> > Config_Map
struct RANGE_INFO range_info;
struct Record_average average_record;
vector<pair<struct Test_Parems, Record_average> > input_output_relation;
INPUT_OUT_MAP input_output_map;

int index_i = 0;
int granularity = 1;

unsigned int Prev_state = 0;

//format:d,c:%u,s:%u,ca:%u,r:%u,o:%u,t:%u
int convert_string_to_elem(string& line, COVG_MAP_VEC& trace, Config_Map& map_config, vector<struct Test_Parems>& test_para_vec)
{
	if (line[0] != 'd' && line[0] != 'a' ) // sanity check: for depart/arrival
		return 1;

	unsigned int cwnd, ssthresh, srtt, rttvar, state, target;
	int64_t curr_time;
	int c_left, s_left, ca_left, r_left, o_left, left, u_left, t_left;

	for (unsigned int i = 0; i < line.size(); i++)
	{
		if (i < line.size() - 2 && line[i] == 'c' && line[i + 1] == ':')
		{
			c_left = i + 2;
		}
		else if (i < line.size() - 2 && line[i] == 's' && line[i + 1] == ':')
		{
			s_left = i + 2;
		}
		else if (i < line.size() - 2 && line[i] == 'a' && line[i + 1] == ':')
		{
			ca_left = i + 2;
		}
		else if (i < line.size() - 2 && line[i] == 'r' && line[i + 1] == ':')
		{
			r_left = i + 2;
		}
		else if (i < line.size() - 2 && line[i] == 'o' && line[i + 1] == ':')
		{
			o_left = i + 2;
		}
		else if (i < line.size() - 2 && line[i] == 't' && line[i + 1] == ':')
		{
			t_left = i + 2;
		}
		else if (i < line.size() - 2 && line[i] == 'u' && line[i + 1] == ':')
		{
			u_left = i + 2;
		}
	}

	cwnd = stol(line.substr(c_left, s_left - 3));
	ssthresh = stol(line.substr(s_left, ca_left - 4));
	//state = stol(line.substr(ca_left, r_left - 3));
	//srtt = stol(line.substr(r_left, o_left - 3));
	//rttvar = stol(line.substr(o_left, t_left - 3));
	//target = stol(line.substr(t_left, u_left - 3));
	//curr_time = stol(line.substr(u_left));

	if (ssthresh == 2147483647) return 0; // remove inital slow start
	if (state == 4) state = 3; // substitute State Recovery(3) with State Loss(4) for state(0-3)
	else if (state == 3) state = 2; // substitute State CWR(2) with State Recovery(3) for state(0-3)
	average_record.cwnd_aver += (cwnd - average_record.cwnd_aver) * 1.0 / (index_i + 1);
	average_record.ssth_aver += (ssthresh - average_record.ssth_aver) * 1.0 / (index_i + 1);
	//average_record.rtt_aver += (srtt - average_record.rtt_aver) * 1.0 / (index_i + 1);
	//average_record.rttvar_aver += (rttvar - average_record.rttvar_aver) * 1.0 / (index_i + 1);
	//average_record.state_aver += (state - average_record.state_aver) * 1.0 / (index_i + 1);
	//average_record.prev_state_aver += (Prev_state - average_record.prev_state_aver) * 1.0 / (index_i + 1);
	//average_record.target_aver += (target - average_record.target_aver) * 1.0 / (index_i + 1);
	index_i++;

	if (cwnd > 0 && cwnd <= CWND_RANGE && ssthresh > 0 && ssthresh <= SSTH_RANGE 
		//&& srtt > 0 && srtt <= RTT_RANGE //&& rttvar > 0 && rttvar <= RTVAR_RANGE 
		//&& state >= 0 && state < STATE_RANGE 
		&& curr_time > 0)  //ssthresh at least 2
	{
		struct State_Record tmp(cwnd, ssthresh, curr_time);
		insert_state(tmp, trace, map_config, test_para_vec);
	}
	
	Prev_state = state;
	return 0;
}

//about 1.5 percent of  2048 total size (given 128 size granularity)
//#define COVG_LIMIT_RANDOM  20
//#define COVG_LIMIT_FEEDBACK1  20
#define GROWTH_SSH 2000
//#define COVG_LIMIT_FEEDBACK2  20
#define TOLERANCE 5
#define INF 99999999999
int total_files = 0;
int prev_coverage_size = 0;
int re_counter = 0;

/*
This function is used to process ns3-dce traces and decide to switch feedbacks
4 inputs, ns-3 datafile name, map coverage vector, ...., test parameter vector
return -1 if unable to open file, 0 if success
*/
int read_from_file(char* filename1, COVG_MAP_VEC& trace, Config_Map& map_config,  vector<struct Test_Parems>& test_para_vec)
{
	string get_line;
	ifstream get_file(filename1);

	if (!get_file.is_open())
	{
		cerr << "Unable to open file:" << filename1 << "\n";
		return -1; //for exception case
	}
	
	//index_i = 0;
	memset(&average_record, 0, sizeof(average_record));

	while (getline(get_file, get_line))
	{
		convert_string_to_elem(get_line, trace, map_config, test_para_vec);
	}
	get_file.close();

	total_files++;

	cout << "Current files:" << total_files << endl;


	return 0 ;
}

/*
The function to check map coverage
*/
int coverage_check(COVG_MAP_VEC& trace){

	if (total_files % 10 == 0) //Check current coverage every 5000 times
	{
		
		double inc_per = trace[0].coverage_map.size() - prev_coverage_size; //get percentage of map coverage growth

		cout << "Every 10 times:" << trace[0].coverage_map.size()  << " ,prev_coverage_size:" << prev_coverage_size << ", growth num count: " << inc_per << " , total files:" << total_files << endl;

		cal_coverage_AllGrans (covg_map_vec);
		prev_coverage_size = trace[0].coverage_map.size(); // Defalut focus on 128 size coverage
		/*
		// could add a switching time; random could be just 10%, feedback apply different %
		if (inc_per < COVG_LIMIT_FEEDBACK2) return 3 ;//to switching for feedback 2
		if (inc_per < COVG_LIMIT_FEEDBACK1) return 2 ;//to switching for feedback 1
		if (inc_per < COVG_LIMIT_RANDOM) return 1 ;//to switching for random
		*/
		if (TOTAL_EXECUTION > 25 && TOTAL_EXECUTION < 35) return 1 ;//to switching for feedback 2
		if (TOTAL_EXECUTION > 15 && TOTAL_EXECUTION < 25) return 1 ;//to switching for feedback 1
        	if (TOTAL_EXECUTION > 5  && TOTAL_EXECUTION < 15 ) return 1 ;//to switching for random
		
		/*
		if (inc_per < INF) {
			
			if(re_counter<5){
				re_counter++;
			}else{
				re_counter = 0;
				return 1;
			}
		}else{
			re_counter = 0;
		}
		*/	
		system("sudo sync && echo 3 | sudo tee /proc/sys/vm/drop_caches"); //used to free system resource
	}
	
	return 0;
	/*
	//Another way is to switch according to execution times
	if (TOTAL_EXECUTION >= 55000 ) return 3 ;//to switching for feedback 2
	if (TOTAL_EXECUTION == 21000 ) return 2 ;//to switching for feedback 1
	if (TOTAL_EXECUTION == 5000 ) return 1 ;//to switching for random
	*/
}

void prepare_before_config_vec(vector<struct Test_Parems>& vec_test_para)
{
	system("rm -r /tmp/output");
	system("mkdir /tmp/output");
	ofstream output_file("/tmp/input_config.txt");

	//TOTAL_EXECUTION++;
	
	//print the input parameter value
	if (output_file)
	{
		output_file << vec_test_para.size() << "\n";
		for (unsigned int i = 0; i < vec_test_para.size(); i++)
		{
			output_file << vec_test_para[i].speed
				<< " " << vec_test_para[i].sftgma.alpha
				<< " " << vec_test_para[i].sftgma.beta
				<< " " << vec_test_para[i].sftgma.shift
				<< " " << vec_test_para[i].Loss_rate
				<< " " << vec_test_para[i].app_speed
				<< " " << vec_test_para[i].rng_run
				<< " " << vec_test_para[i].curr_time
				<< "\n";
			if (DEBUG)
				cout << vec_test_para[i].speed
					<< " " << vec_test_para[i].sftgma.alpha
					<< " " << vec_test_para[i].sftgma.beta
					<< " " << vec_test_para[i].sftgma.shift
					<< " " << vec_test_para[i].Loss_rate
					<< " " << vec_test_para[i].app_speed
					<< " " << vec_test_para[i].rng_run
					<< " " << vec_test_para[i].curr_time
					<< "\n";

		}
		output_file.close();
	}
	else
	{
		cout << "[Error] Canot output config vec!" << endl;
		return ;
	}

	system("cp /tmp/input_config.txt /tmp/output/"); // Record this input paremeter

}


int execute_ns3_2(int mode)
{
	char cmd[512];
	snprintf (cmd, 512, "sudo sh dce.sh dce-linux %d", TOTAL_EXECUTION);

	if (DEBUG) cout << cmd << endl;
	
	int res = system(cmd);
	if (res < 0 || res == 127)
	{
		cerr << "cannot execute the ns3 script sucessfully, retval:" << res << endl;
		return -1;
	}

	return 0;
}

/*
run ns-3 and check for return status
-1 is fails, 0 is success and no switch, >0 is saturate and switch mode
*/
int try_per_config(int i, int mode) {	//test input i
	
	char mvcmd[256] = {0};
	
	TOTAL_EXECUTION++;
	
	//set test input parameters
	struct Test_Parems test_para;
	vector<struct Test_Parems> test_para_vec;
	
	int res = 0;
	
	//get random value, the random algorithm is from gsl
	random_input_value(test_para);
	
	test_para.rng_run = TOTAL_EXECUTION;
	test_para_vec.push_back(test_para);
	
	//print the value of the inputs
	prepare_before_config_vec(test_para_vec);
	
	//launch ns3
	/*for (int j = 1; j <= TRIES_PER; j++){
		
		res = execute_ns3_2(0); //mode 0
	}*/
	res = execute_ns3_2(0);
	
	if (res == -1){		//if execution fails
	
		TOTAL_EXECUTION--; //offsetting this execution
		//return -2; // used for skip exception
	}
	else{		//when ns3 success
	
		//config ns3 output, check map coverage
		snprintf (mvcmd, 256, "/tmp/output/%d/messages", TOTAL_EXECUTION);
		if(read_from_file(mvcmd, covg_map_vec, config_map, test_para_vec)==0) 
			res = coverage_check(covg_map_vec);

		//mv output to output_all
		snprintf (mvcmd, 256, "mv /tmp/output /tmp/output_all/config_%d", TOTAL_EXECUTION);
		system(mvcmd);

		if (DEBUG)
		{
			cout << "Input Test_Parems:" << endl;
			test_para.print();
		}

		input_output_relation.push_back(make_pair(test_para, average_record));
	
	}
	/*
	//print output values
	snprintf (mvcmd, 256, "/tmp/output/%d/messages", TOTAL_EXECUTION);
	res = read_from_file(mvcmd, covg_map_vec, config_map, test_para_vec); 

	//after
	snprintf (mvcmd, 256, "mv /tmp/output /tmp/output_all/config_%d", TOTAL_EXECUTION);
	system(mvcmd);

	if (DEBUG)
	{
		cout << "Input Test_Parems:" << endl;
		test_para.print();
	}

	input_output_relation.push_back(make_pair(test_para, average_record));
	*/
	return res;
}

int cmd_init_random()
{
	system("sudo rm -r /tmp/output_all");
	system("sudo rm -r /tmp/input_config.txt");
	system("sudo mkdir /tmp/output_all");
	return 0;
}

int cmd_init_feedback() // Todo: change mode
{
	system("sudo rm -r /tmp/input_config.txt");
	system("sudo rm -r /tmp/output");
	system("sudo rm -r /tmp/output_feedback1");
	system("sudo rm -r /tmp/output_feedback2");
	system("sudo mkdir /tmp/output_feedback1");
	system("sudo mkdir /tmp/output_feedback2");
	return 0;
}

int purely_random_testing(int mode)
{
	int res = 0;
	int i = 1;
	while(1)
	{
		cout << "The number of executions " << TOTAL_EXECUTION << endl;
		res = try_per_config(i, mode);
		if (res >  0)// random testing saturated
		{
			cout << "Random switching at: " << TOTAL_EXECUTION << endl;
			break;
		}
		i++;
	}
	return res;
}

int init_coverage_map_vec()
{
	int granularity = 0;
	for (int i = 0; i < 10; i ++) // Now we have coverage with size of 1, 2, 4, 8, 16, 32 ...... 512
	{
		Grans_coverage_map coverage;
		granularity = pow(2, i);
		if (DEBUG) std::cout << "coverage map initialization, granularity:" << granularity  << endl;

		struct RANGE_INFO range_info;
		cal_range(range_info, granularity);
		coverage.granularity = granularity;
		coverage.range_info = range_info;
		covg_map_vec.push_back(coverage);
	}

	return 0;
}

/*
Output folder: 
/tmp/output_all  		for intal random testing
/tmp/output_feedback1   for feedback1
/tmp/output_feedback2   for feedback2
*/
int main (int argc, char* argv[])
{
	freopen("log.txt", "w", stdout); // Log file will generated to log.txt
	MAIL_MODE = 0; //default is disable
	random_init(); // init for random generator
	//cout << "free up system memory for this experiment !\n";
	system("sudo sync && echo 3 | sudo tee /proc/sys/vm/drop_caches");

	//if (MAIL_MODE) system ("sudo mail -s \"Experiment Begin Mention \" xxx@example.com < /dev/null ");

	//char cmd[512];

	init_coverage_map_vec();
	cout << "System initiation succeed, ACT starts!" << endl;
	cout << "Search guide: [Stage] [Coverage] [Caution] [Error]" << endl;
	cout << "[Stage] Purely random: " << endl;
	cmd_init_random();
	purely_random_testing(0);
	pearson_corrleation(input_output_relation, input_output_map); // To get pearson corrleation

	//cout << "[Purely Random] 5d coverage:" << endl;
	//cal_coverage_AllGrans(covg_map_vec);

	cmd_init_feedback();
	
	
	cout << "[Stage] Begin feedback 1: " << endl;
	feedback_random_N(1, covg_map_vec, config_map, input_output_map);

	//cal_coverage_AllGrans(covg_map_vec);

	system("sudo sync && echo 3 | sudo tee /proc/sys/vm/drop_caches");
	
	cout << "[Stage] Begin feedback 2: " << endl;
	feedback_random_N(2, covg_map_vec, config_map, input_output_map);
	
	//After feedback coverage
	cout << "[Stage] Finish succeed!" << endl;
	cout << "TOTAL_EXECUTION:" << TOTAL_EXECUTION << endl;
	cal_coverage_AllGrans(covg_map_vec);

	//if (MAIL_MODE) system ("sudo mail -s \"Experiment End Mention \" xxx@example.com < /dev/null ");

	exit(0);
}




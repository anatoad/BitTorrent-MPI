#ifndef __UTILS_H__
#define __UTILS_H__

#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <cstring>
#include <string>
#include <vector>
#include <list>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define MAX_MESSAGE_LEN 40000

#define TAG_ACK 1
#define TAG_FIN 2
#define TAG_REQUEST 3
#define TAG_SEGMENT 4
#define TAG_UPDATE 5
#define TAG_PEER_LIST 6
#define TAG_FILE_DOWNLOADED 7
#define TAG_CLIENT_INIT 8

using namespace std;

typedef struct {
    char hash[HASH_SIZE+1];
    bool owned;
} seg_info;

typedef struct {
    int seg_no;
    char hash[HASH_SIZE+1];
} segment_info;

void send_file_request(const char *filename, char *hash, int segment);

void send_file_downloaded(const char *filename);

void parse_update(char *data, int source, map<string, map<int, vector<segment_info>>> &tracker_map);

void parse_list_from_tracker(map<string, map<int, list<int>>> &mp, char *data, map<string, map<int, seg_info>> &files);

void send_request_to_tracker(map<string, map<int, seg_info>> &files, map<string, int> &num_segments, map<string, int> &missing_segments);

void add_files_to_map(char *data, int rank, map<string, map<int, vector<segment_info>>> &tracker_map);

void send_peer_list(char *request, int dest, map<string, map<int, vector<segment_info>>> &tracker_map);

int find_peer(map<string, map<int, list<int>>> &mp, string filename, int segment, map<int, int> &peer_requests);

void update_missing_segments(map<string, map<int, list<int>>> &mp, map<string, int> &num_segments,map<string, int> &missing_segments, int *needed_segments);

void print_info_to_file(int rank, string filename, map<string, int> &num_segments, map<string, map<int, seg_info>> &files);

#endif
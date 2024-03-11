#include "utils.h"

/* Parse initial message from the client and add all the files information to the tracker map. */
void add_files_to_map(char *data, int rank, map<string, map<int, vector<segment_info>>> &tracker_map) {
    char filename[MAX_FILENAME+1] = {0};
    char hash[HASH_SIZE+1] = {0};

    int num_files = *(int *)data;
    int offset = sizeof(int);

    int num_segments;
    segment_info segment;

    for (int i = 0; i < num_files; i++) {
        /* get the filename and number of segments of the file */
        strncpy(filename, data + offset, MAX_FILENAME+1);
        offset += MAX_FILENAME+1;

        num_segments = *(int *)(data + offset);
        offset += sizeof(int);

        /* store hash and segment number for each segment of the file in tracker_map */
        for (int j = 0; j < num_segments; j++) {
            strncpy(hash, data + offset, HASH_SIZE+1);
            offset += HASH_SIZE+1;

            segment.seg_no = j;
            strncpy(segment.hash, hash, HASH_SIZE+1);

            tracker_map[filename][rank].push_back(segment);
        }
    }
}

/* Request format (from client to client):
size(bytes):  MAX_FILENAME  |   HASH_SIZE
               filename     |  segment hash  */
void send_file_request(const char *filename, char *hash, int dest) {
    char data[MAX_MESSAGE_LEN] = {0};

    strncpy(data, filename, MAX_FILENAME+1);
    strncpy(data + MAX_FILENAME+1, hash, HASH_SIZE+1);

    MPI_Send(data, MAX_MESSAGE_LEN, MPI_CHAR, dest, TAG_REQUEST, MPI_COMM_WORLD);
}

/* Message from client to the tracker informing download finished for file with filename. */
void send_file_downloaded(const char *filename) {
    char data[MAX_MESSAGE_LEN+1] = {0};
    MPI_Send(data, MAX_MESSAGE_LEN, MPI_CHAR, TRACKER_RANK, TAG_FILE_DOWNLOADED, MPI_COMM_WORLD);
}

/* Update tracker_map with data received from the client. */
void parse_update(char *data, int source, map<string, map<int, vector<segment_info>>> &tracker_map) {
    char filename[MAX_FILENAME+1];
    char hash[HASH_SIZE+1];
    int segment;
    segment_info info;

    int num_segments = *(int *)data;
    int offset = sizeof(int);

    for (int i = 0; i < num_segments; i++) {
        /* get filename, segment number and hash from the message */
        strncpy(filename, data + offset, MAX_FILENAME+1);
        offset += MAX_FILENAME+1;

        segment = *(int *)(data + offset);
        offset += sizeof(int);

        strncpy(hash, data + offset, HASH_SIZE+1);
        offset += HASH_SIZE+1;

        info.seg_no = segment;
        strncpy(info.hash, hash, HASH_SIZE+1);

        /* update tracker information */
        tracker_map[filename][source].push_back(info);
    }
}

/* Parse the list of files from the tracker, build the mp map.
   mp[filename][rank] = {list of segments of file with filename owned by peer with given rank}
*/
void parse_list_from_tracker(map<string, map<int, list<int>>> &mp, char *data, map<string, map<int, seg_info>> &files) {
    char filename[MAX_FILENAME+1];
    int num_peers, rank, num_seg, segment_number, total_segments;
    seg_info info_segment;

    int num_files = *((int *)data);
    int offset = sizeof(int);

    for (int i = 0; i < num_files; i++) {
        /* get filename */
        strncpy(filename, data + offset, MAX_FILENAME+1);
        offset += MAX_FILENAME+1;

        /* get file information (number of segments + hash of each segment) */
        total_segments = *(int *)(data + offset);
        offset += sizeof(int);

        for (int j = 0; j < total_segments; j++) {
            strncpy(info_segment.hash, data + offset, HASH_SIZE+1);
            offset += HASH_SIZE +1;
    
            info_segment.owned = false;

            if (files[filename].find(j) == files[filename].end())
                files[filename][j] = info_segment;
        }

        /* get peers and which segments each one has */
        num_peers = *(int *)(data + offset);
        offset += sizeof(int);

        for (int p = 0; p < num_peers; p++) {
            rank = *(int *)(data + offset);
            offset += sizeof(int);

            num_seg = *(int *)(data + offset);
            offset += sizeof(int);

            for (int j = 0; j < num_seg; j++) {
                segment_number = *(int *)(data + offset);
                offset += sizeof(int);

                mp[filename][rank].push_back(segment_number);
            }
        }
    }
}

/* Request format (from client to tracker):
size(bytes):    sizeof(int)        | MAX_FILENAME  | MAX_FILENAME |  ...  |  MAX_FILENAME 
        N = number of wanted files |   filename1   |   filename2  |  ...  |  filenameN    */
void send_request_to_tracker(map<string, map<int, seg_info>> &files, map<string, int> &num_segments, map<string, int> &missing_segments) {
    char data[MAX_MESSAGE_LEN] = {0};
    int offset = sizeof(int);
    int num_files = 0;

    /* add the files that have missing segments */
    for (auto &[filename, m]: files) {
        if (num_segments[filename] == 0 || missing_segments[filename] > 0) {
            num_files++;

            strncpy(data + offset, filename.c_str(), MAX_FILENAME+1);
            offset += MAX_FILENAME+1;
        }
    }

    /* add the number of files as the first bytes in the request */
    strncpy(data, (char *)(&num_files), sizeof(int));

    MPI_Send(data, MAX_MESSAGE_LEN, MPI_CHAR, TRACKER_RANK, TAG_REQUEST, MPI_COMM_WORLD);
}

/* Response from tracker to client (with peers list). */
void send_peer_list(char *request, int dest, map<string, map<int, vector<segment_info>>> &tracker_map) {
    char data[MAX_MESSAGE_LEN];
    char filename[MAX_FILENAME+1];

    /* parse request and build response list */
    int offset = sizeof(int);
    int req_offset = sizeof(int);
    int num_peers, num_seg;

    /* total number of files */
    int num_files = *(int *)request;
    strncpy(data, (char *)(&num_files), sizeof(int));

    for (int i = 0; i < num_files; i++) {
        strncpy(filename, request + req_offset, MAX_FILENAME+1);

        /* copy filename */
        strncpy(data + offset, filename, MAX_FILENAME+1);
        offset += MAX_FILENAME+1;

        /* copy segments information */
        int total_segments = 0, seed_rank;
        for (auto &[rank, list]: tracker_map[filename]) {
            if ((int)list.size() > total_segments) {
                total_segments = list.size();
                seed_rank = rank;
            }
        }

        strncpy(data + offset, (char *)&total_segments, sizeof(int));
        offset += sizeof(int);

        for (int j = 0; j < total_segments; j++) {
            strncpy(data + offset, tracker_map[filename][seed_rank][j].hash, HASH_SIZE+1);
            offset += HASH_SIZE+1;
        }

        /* count number of peers */
        num_peers = tracker_map[filename].size();
        strncpy(data + offset, (char *)(&num_peers), sizeof(int));
        offset += sizeof(int);

        for (auto &[rank, list]: tracker_map[filename]) {
            /* rank of peer */
            strncpy(data + offset, (char *)(&rank), sizeof(int));
            offset += sizeof(int);

            /* number of segments of the file */
            num_seg = list.size();
            strncpy(data + offset, (char *)(&num_seg), sizeof(int));
            offset += sizeof(int);

            /* list of segment numbers */
            for (segment_info &info: list) {
                strncpy(data + offset, (char *)(&info.seg_no), sizeof(int));
                offset += sizeof(int);
            }
        }

        req_offset += MAX_FILENAME+1;
    }

    MPI_Send(data, MAX_MESSAGE_LEN, MPI_CHAR, dest, TAG_PEER_LIST, MPI_COMM_WORLD);
}

/* Finds rank of a peer from which to request the segment. Uses peer_requests map to find the peer with the
least amount of requests sent from current client, so as to vary the peers as much as possible. */
int find_peer(map<string, map<int, list<int>>> &mp, string filename, int segment, map<int, int> &peer_requests) {
    /* find a peer that has the segment */
    int peer_rank = 0, requests = 0;

    /* get the peer with the least number of requests for variation */
    for (auto &[rank, list] : mp[filename]) {
        for (int &s : list) {
            if (s == segment) {
                if (peer_requests.find(rank) == peer_requests.end()) {
                    peer_rank = rank;
                    peer_requests[rank] = 1;
                    return peer_rank;

                } else if (peer_rank == 0 || peer_requests[rank] < requests) {
                    peer_rank = rank;
                    requests = peer_requests[rank];
                }
                break;
            }
        }
    }

    /* update number of requests sent to the peer */
    peer_requests[peer_rank] = requests + 1;

    return peer_rank;
}

/* Update the number of missing segments for each file. */
void update_missing_segments(map<string, map<int, list<int>>> &mp, map<string, int> &num_segments, map<string, int> &missing_segments, int *needed) {
    int total_segments;

    for (auto &[filename, m]: mp) {
        if (num_segments[filename] == 0) {
            /* get the number of segments of the file (the list with the most elements - list of the seed) */
            total_segments = 0;
            for (auto &[rank, list]: m) {
                total_segments = max(total_segments, (int)list.size());
            }

            num_segments[filename] = total_segments;
            missing_segments[filename] = total_segments;
            *needed = *needed + total_segments;
        }
    }
}

/* Print segments hash in order to the output file. */
void print_info_to_file(int rank, string filename, map<string, int> &num_segments, map<string, map<int, seg_info>> &files) {
    char output_filename[100] = {0};
    sprintf(output_filename, "client%d_%s", rank, filename.c_str());

    FILE *fp = fopen(output_filename, "a+");

    for (int sn = 0; sn < num_segments[filename]; sn++) {
        fprintf(fp, "%s\n", files[filename][sn].hash);
    }

    fclose(fp);
}

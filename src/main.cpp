#include "utils.h"

/* tracker data */
/* tracker_map[filename][rank] = a vector of elements containing {segment_number, segment_hash}
for each segment owned by client with given rank */
map<string, map<int, vector<segment_info>>> tracker_map;

/* client data */
map<string, int> num_segments;
map<string, int> missing_segments;
map<int, int> peer_requests;

/* files[filename][segment_number] = element of type {hash, owned} for each segment of the file */
map<string, map<int, seg_info>> files;

void init_tracker(int numtasks, int rank) {
    char data[MAX_MESSAGE_LEN];
    MPI_Status status;

    /* Wait for initial message from each client, containing the list of owned files. */
    for (int i = 0; i < numtasks - 1; i++) {
        MPI_Recv(data, MAX_MESSAGE_LEN, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

        add_files_to_map(data, status.MPI_SOURCE, tracker_map);
    }

    /* Send ACK to each client */
    memset(data, 0, sizeof(data));
    strncpy(data, "ACK", 4);
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(data, MAX_MESSAGE_LEN, MPI_CHAR, i, TAG_ACK, MPI_COMM_WORLD);
    }
}

void tracker(int numtasks, int rank) {
    init_tracker(numtasks, rank);

    char data[MAX_MESSAGE_LEN];
    MPI_Status status;

    /* run until all clients have finished downloading their desired files */
    int finished = 0;

    while (finished < numtasks - 1) {
        MPI_Recv(data, MAX_MESSAGE_LEN, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_FIN) {
            finished++;

        } else if (status.MPI_TAG == TAG_REQUEST) {
            send_peer_list(data, status.MPI_SOURCE, tracker_map);

        } else if (status.MPI_TAG == TAG_UPDATE) {
            parse_update(data, status.MPI_SOURCE, tracker_map);
        }
    }

    /* send FIN to each client so they can stop */
    memset(data, 0, sizeof(data));
    strncpy(data, "FIN", 4);
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(data, MAX_MESSAGE_LEN, MPI_CHAR, i, TAG_REQUEST, MPI_COMM_WORLD);
    }
}

void init_client(int rank) {
    char input_filename[MAX_FILENAME+1];
    char filename[MAX_FILENAME+1] = {0};
    char hash[HASH_SIZE+1] = {0};
    char data[MAX_MESSAGE_LEN] = {0};

    sprintf(input_filename, "in%d.txt", rank);

    FILE *fp = fopen(input_filename, "r");

    /* read the list of owned files and their segments */
    int num_files, total_segments;
    fscanf(fp, "%d", &num_files);

    strncpy(data, (char *)(&num_files), sizeof(int));
    int offset = sizeof(int);

    seg_info segment_info;

    for (int i = 0; i < num_files; i++) {
        fscanf(fp, "%s %d", filename, &total_segments);

        strncpy(data + offset, filename, MAX_FILENAME+1);
        offset += MAX_FILENAME+1;

        strncpy(data + offset, (char *)&total_segments, sizeof(int));
        offset += sizeof(int);

        num_segments[filename] = total_segments;
        missing_segments[filename] = 0;

        for (int j = 0; j < total_segments; j++) {
            fscanf(fp, "%s", hash);

            strncpy(data + offset, hash, HASH_SIZE+1);
            offset += HASH_SIZE+1;

            strncpy(segment_info.hash, hash, HASH_SIZE+1);
            segment_info.owned = true;

            files[filename][j] = segment_info;
        }
    }

    /* read the list of wanted files */
    int num_wanted;
    fscanf(fp, "%d", &num_wanted);

    for (int i = 0; i < num_wanted; i++) {
        map<int, seg_info> emptyMap;
        fscanf(fp, "%s", filename);

        num_segments[filename] = 0;
        missing_segments[filename] = 0;

        files[filename] = emptyMap;
    }

    fclose(fp);

    /* Send the file list to the tracker */
    MPI_Send(data, MAX_MESSAGE_LEN, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

    /* Wait for ACK from the tracker */
    MPI_Status status;
    MPI_Recv(data, MAX_MESSAGE_LEN, MPI_CHAR, TRACKER_RANK, TAG_ACK, MPI_COMM_WORLD, &status);
}

void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;

    int segment;
    char data[MAX_MESSAGE_LEN+1];
    char update[MAX_MESSAGE_LEN+1];

    int needed_segments = 0, total_segments;
    MPI_Status status;

    do {
        /* send request to the tracker */
        send_request_to_tracker(files, num_segments, missing_segments);

        /* receive list with seeds/peers from the tracker */
        MPI_Recv(data, MAX_MESSAGE_LEN, MPI_CHAR, TRACKER_RANK, TAG_PEER_LIST, MPI_COMM_WORLD, &status);

        map<string, map<int, list<int>>> mp;
        parse_list_from_tracker(mp, data, files);


        /* update the number of missing segments */
        update_missing_segments(mp, num_segments, missing_segments, &needed_segments);

        total_segments = min(needed_segments, 10);

        memset(update, 0, sizeof(update));
        strncpy(update, (char *)&total_segments, sizeof(int));
        int offset = sizeof(int);

        /* send requests to get segments from peers */
        for (int i = 0; i < total_segments; i++) {
            /* get missing segment */
            for (auto &[filename, m]: files) {
                if (missing_segments[filename] > 0) {
                    for (int s = 0; s < num_segments[filename]; s++) {
                        if (files[filename][s].owned == false) {
                            segment = s;
                            break;
                        }
                    }

                    /* find a peer who has the segment */
                    int peer_rank = find_peer(mp, filename, segment, peer_requests);

                    /* send request */
                    send_file_request(filename.c_str(), files[filename][segment].hash, peer_rank);

                    /* receive segment */
                    MPI_Recv(data, MAX_MESSAGE_LEN, MPI_CHAR, peer_rank, TAG_ACK, MPI_COMM_WORLD, &status);

                    /* update file list */
                    files[filename][segment].owned = true;
                    missing_segments[filename]--;

                    if (missing_segments[filename] == 0) {
                        /* client finished downloading file, send message to tracker */
                        send_file_downloaded(filename.c_str());

                        /* print information to file */
                        print_info_to_file(rank, filename, num_segments, files);
                    }

                    /* add filename, segment number and segment hash to the update message */
                    strncpy(update + offset, filename.c_str(), MAX_FILENAME+1);
                    offset += MAX_FILENAME+1;

                    strncpy(update + offset, (char *)&segment, sizeof(int));
                    offset += sizeof(int);

                    strncpy(update + offset, files[filename][segment].hash, HASH_SIZE+1);
                    offset += HASH_SIZE+1;

                    /* update needed segments count */
                    needed_segments--;
                    break;
                }
            }
        }

        /* send update to tracker */
        MPI_Send(update, MAX_MESSAGE_LEN, MPI_CHAR, TRACKER_RANK, TAG_UPDATE, MPI_COMM_WORLD);
    } while (needed_segments > 0);

    /* client finished downloading all files, send fin to tracker */
    MPI_Send(data, MAX_MESSAGE_LEN, MPI_CHAR, TRACKER_RANK, TAG_FIN, MPI_COMM_WORLD);

    return NULL;
}

void *upload_thread_func(void *arg) {
    char data[MAX_MESSAGE_LEN];
    MPI_Status status;

    while (true) {
        /* receive message */
        MPI_Recv(data, MAX_MESSAGE_LEN, MPI_CHAR, MPI_ANY_SOURCE, TAG_REQUEST, MPI_COMM_WORLD, &status);

        /* check if message received is FIN from the tracker */
        if (!strncmp(data, "FIN", 3))
            break;

        /* send ACK */
        MPI_Send(data, MAX_MESSAGE_LEN, MPI_CHAR, status.MPI_SOURCE, TAG_ACK, MPI_COMM_WORLD);
    }

    return NULL;
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    /* Initialize client */
    init_client(rank);

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}

int main (int argc, char *argv[]) {
    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);

    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}

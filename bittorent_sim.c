#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define MAX_CLIENTS 8

#define INIT_SEED_NO_TAG 0
#define INIT_FILE_NO_TAG 1
#define INIT_FILE_SEGMENT_NO_TAG 2
#define INIT_FILE_SEGMENT_POS_TAG 3
#define INIT_FILE_SEGMENT_TAG 4
#define INIT_FILE_SEGMENT_AVAILABLE 5
#define INIT_ACK_TAG 6

#define MSG_RANK_TAG 7
#define MSG_COMMAND_TAG 8
#define MSG_FILE_NO_TAG 9

#define RQ_CLIENTS_TAG 10
#define RQ_SEGMENTS_NO_TAG 11
#define RQ_SEGMENTS_TAG 12
#define RQ_TRAFFIC_TAG 13

#define DWD_RANK_TAG 14
#define DWD_FILE_NO_TAG 15
#define DWD_SEGMENT_TAG 16
#define DWD_HASH_TAG 17
#define DWD_POSITION_SEGMENT_TAG 18

#define UPD_SEGMENTS_NO_TAG 19
#define UPD_TRAFFIC_TAG 20
#define UPD_STATUS_TAG 21
#define UPD_SEGMENT_POS_TAG 22
#define UPD_HASH_TAG 23

#define UPLOAD_COMM_TAG 24

#define INITIAL_SEEDS_SIZE 8
#define INITIAL_PEERS_SIZE 8

#define UNDEFINED_STATUS 0
#define SEED 1
#define PEER 2
#define LEECHER 3

#define MAX_DOWNLOADS 10

// Structure used to keep segment data of a file
struct File_Data_Segment {
    int hash_pos;
    char *hash;
};

// Strucutre used to keep data of a file
struct File_Data {
    int file_status;
    int file_traffic;
    int file_segm_no;
    int *available_segments;
    struct File_Data_Segment *file_segments;
};

// Structure used by each client to keep data of each file
struct Client_Files {
    int files_no;
    struct File_Data *files;
};

// Structure used by tracker to keep data of each file
struct File_Swarm {
    int total_file_segm_no;
    int *file_owners;
    struct File_Data *clients_file_data;
};

// Structure used to communicate between tracker and client
struct RQ_File_Info {
    int total_file_segm_no;
    int *file_owners;
    int **segments_owners;
};

// Global variables
struct Client_Files *client_files;

int Numtasks = 0;

// Function that requests information of a specific file (request file)
void peer_rf_comm(struct RQ_File_Info *files_info, int rank, int file_no) {
    int command = 0;
    // Send client rank, command and file number
    MPI_Send(&rank, 1, MPI_INT, TRACKER_RANK, MSG_RANK_TAG, MPI_COMM_WORLD);
    MPI_Send(&command, 1, MPI_INT, TRACKER_RANK, MSG_COMMAND_TAG, MPI_COMM_WORLD);
    MPI_Send(&file_no, 1, MPI_INT, TRACKER_RANK, MSG_FILE_NO_TAG, MPI_COMM_WORLD);

    MPI_Status status;
    // Receive owners of the file (seeds or peers)
    MPI_Recv(files_info[file_no].file_owners, Numtasks, MPI_INT, TRACKER_RANK, RQ_CLIENTS_TAG, MPI_COMM_WORLD, &status);

    // Receive number of total segments the file has
    MPI_Recv(&files_info[file_no].total_file_segm_no, 1, MPI_INT, TRACKER_RANK, RQ_SEGMENTS_NO_TAG, MPI_COMM_WORLD, &status);

    // For each client that ownes segments from the file, receive what segments ownes
    for (int i = 1; i < Numtasks; i++) {
        if (files_info[file_no].file_owners[i] == 1) {
            MPI_Recv(files_info[file_no].segments_owners[i], MAX_CHUNKS, MPI_INT, TRACKER_RANK, RQ_SEGMENTS_TAG, MPI_COMM_WORLD, &status);
        }
    }
}

// Function that sends update of a specific file to the tracker (update file)
void peer_uf_comm(int rank, int file_no) {
    int command = 1;
    // Send client rank, command and file number
    MPI_Send(&rank, 1, MPI_INT, TRACKER_RANK, MSG_RANK_TAG, MPI_COMM_WORLD);
    MPI_Send(&command, 1, MPI_INT, TRACKER_RANK, MSG_COMMAND_TAG, MPI_COMM_WORLD);
    MPI_Send(&file_no, 1, MPI_INT, TRACKER_RANK, MSG_FILE_NO_TAG, MPI_COMM_WORLD);
    
    // Send number of segments populated
    MPI_Send(&client_files->files[file_no].file_segm_no, 1, MPI_INT, TRACKER_RANK, UPD_SEGMENTS_NO_TAG, MPI_COMM_WORLD);

    // Send status of the file owned by the client (PEER/LEECHER or SEED)
    MPI_Send(&client_files->files[file_no].file_status, 1, MPI_INT, TRACKER_RANK, UPD_STATUS_TAG, MPI_COMM_WORLD);

    // Send position and hash for last 10 segments populated
    for (int i = client_files->files[file_no].file_segm_no - MAX_DOWNLOADS; i < client_files->files[file_no].file_segm_no; i++) {
        MPI_Send(&i, 1, MPI_INT, TRACKER_RANK, UPD_SEGMENT_POS_TAG, MPI_COMM_WORLD);
        MPI_Send(client_files->files[file_no].file_segments[i].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, UPD_HASH_TAG, MPI_COMM_WORLD);
    }
}

// Function that announces finishing of dowloading a specific file (finish file)
void peer_ff_comm(int rank, int file_no) {
    // send last updates to the tracker
    peer_uf_comm(rank, file_no);

    int command = 2;
    // Send client rank, command and file number
    MPI_Send(&rank, 1, MPI_INT, TRACKER_RANK, MSG_RANK_TAG, MPI_COMM_WORLD);
    MPI_Send(&command, 1, MPI_INT, TRACKER_RANK, MSG_COMMAND_TAG, MPI_COMM_WORLD);
    MPI_Send(&file_no, 1, MPI_INT, TRACKER_RANK, MSG_FILE_NO_TAG, MPI_COMM_WORLD);
}

// Function that announces finishing of dowloading all files (finish all files)
void peer_fa_comm(int rank) {
    int command = 3;
    // Send client rank and command
    MPI_Send(&rank, 1, MPI_INT, TRACKER_RANK, MSG_RANK_TAG, MPI_COMM_WORLD);
    MPI_Send(&command, 1, MPI_INT, TRACKER_RANK, MSG_COMMAND_TAG, MPI_COMM_WORLD);
}

// Function that search for a client who ownes the wanted segment and low traffic on it
int peer_find_client(struct RQ_File_Info *files_info, int send_rq_to, int rank, int file_no, int segment_wanted_pos) {
    MPI_Status status;
    int min_traffic_info = MAX_CLIENTS + 1;

    // choose seed or peer for segment wanted
    for (int cl_rank = 1; cl_rank < Numtasks; cl_rank++) {
        if (cl_rank != rank) {
            // check if client ownes the file or a part of it
            if (files_info[file_no].file_owners[cl_rank] == 1) {
                // check if client found has wanted segment
                if (files_info[file_no].segments_owners[cl_rank][segment_wanted_pos] == 1) {
                    // request traffic data for the file wanted on client found
                    int comm = 2;
                    MPI_Send(&rank, 1, MPI_INT, cl_rank, DWD_RANK_TAG, MPI_COMM_WORLD);
                    MPI_Send(&comm, 1, MPI_INT, cl_rank, UPLOAD_COMM_TAG, MPI_COMM_WORLD);
                    MPI_Send(&file_no, 1, MPI_INT, cl_rank, DWD_FILE_NO_TAG, MPI_COMM_WORLD);

                    //receive traffic information
                    int traffic_info;
                    MPI_Recv(&traffic_info, 1, MPI_INT, cl_rank, UPD_TRAFFIC_TAG, MPI_COMM_WORLD, &status);

                    // check if the current client has less traffic than the previous one
                    if (traffic_info < min_traffic_info) {
                        comm = 3;
                        // if this client was already connected to another client, ask for breaking connection
                        if (send_rq_to > -1) {
                            MPI_Send(&rank, 1, MPI_INT, send_rq_to, DWD_RANK_TAG, MPI_COMM_WORLD);
                            MPI_Send(&comm, 1, MPI_INT, send_rq_to, UPLOAD_COMM_TAG, MPI_COMM_WORLD);
                            MPI_Send(&file_no, 1, MPI_INT, send_rq_to, DWD_FILE_NO_TAG, MPI_COMM_WORLD);
                        }

                        // set the new client and the minimum traffic
                        send_rq_to = cl_rank;
                        min_traffic_info = traffic_info;
                    }

                }
            }
        }
    }

    // return the client who ownes the segment wanted and also has the minimum traffic for the file
    return send_rq_to;
}

// Function that allocates memory for the structure RQ_FILE_INFO in order to send and receive information from tracker
struct RQ_File_Info *init_files_info() {
    struct RQ_File_Info *files_info = malloc((MAX_FILES + 1) * sizeof(struct RQ_File_Info));

    for (int i = 1; i <= MAX_FILES; i++) {
        files_info[i].file_owners = calloc(Numtasks, sizeof(int));
        files_info[i].segments_owners = calloc(Numtasks, sizeof(int *));

        for (int j = 1; j < Numtasks; j++) {
            files_info[i].segments_owners[j] = calloc(MAX_CHUNKS, sizeof(int));
        }
    }

    return files_info;
}

// Function that eliberates the memory allocated for structure "RQ_File_Info"
void destroy_files_info(struct RQ_File_Info *files_info) {
    if (files_info != NULL) {
        for (int i = 1; i <= MAX_FILES; i++) {
            if (files_info[i].file_owners != NULL) {
                free(files_info[i].file_owners);
                files_info[i].file_owners = NULL;
            }
            if (files_info[i].segments_owners != NULL) {
                for (int j = 1; j < Numtasks; j++) {
                    if (files_info[i].segments_owners[j] != NULL) {
                        free(files_info[i].segments_owners[j]);
                        files_info[i].segments_owners[j] = NULL;
                    }
                }
                free(files_info[i].segments_owners);
                files_info[i].segments_owners = NULL;
            }
        }
        free(files_info);
        files_info = NULL;
    }
}

// Function that downloads segments missing from the file wanted
void *download_thread_func(void *arg) {
    MPI_Status status;

    int rank = *(int*) arg;

    // initialize structure used to send and receive data about files from the tracker
    struct RQ_File_Info *files_info = init_files_info();

    // number of segments downloaded by the client for each file
    int downloads_counter[MAX_FILES + 1] = {0};

    int send_rq_to = -1;

    while (1) {     
        // variable used to memories how many files are still PEER or LEECHER
        int not_finished_files = 0;

        // go trough all files
        for (int file_no = 1; file_no <= MAX_FILES; file_no++) {
            // segments download process for files that are not considered yet "SEEDs"
            if (client_files->files[file_no].file_status == PEER || client_files->files[file_no].file_status == LEECHER) {                
                // send updates after every 10 segments downloaded for a file
                if (downloads_counter[file_no] == MAX_DOWNLOADS) {
                    peer_uf_comm(rank, file_no);

                    // reset number of file downloaded
                    downloads_counter[file_no] = 0;
                }

                // increment number of PEER or LEECHER
                not_finished_files++;
                
                // request last updates for the file
                peer_rf_comm(files_info, rank, file_no);

                // determine which segment is needed
                int segment_wanted_pos = client_files->files[file_no].file_segm_no;
                
                // determine which client ownes the segment from the specific file
                send_rq_to = peer_find_client(files_info, send_rq_to, rank, file_no, segment_wanted_pos);

                // if no SEED or PEER found
                if (send_rq_to == -1)
                    continue;

                int comm = 0;

                // send rank
                MPI_Send(&rank, 1, MPI_INT, send_rq_to, DWD_RANK_TAG, MPI_COMM_WORLD);

                // send command 0 - segment request
                MPI_Send(&comm, 1, MPI_INT, send_rq_to, UPLOAD_COMM_TAG, MPI_COMM_WORLD);

                // send file number
                MPI_Send(&file_no, 1, MPI_INT, send_rq_to, DWD_FILE_NO_TAG, MPI_COMM_WORLD);

                // send segment wanted
                MPI_Send(&segment_wanted_pos, 1, MPI_INT, send_rq_to, DWD_SEGMENT_TAG, MPI_COMM_WORLD);

                // receive from the peer/seed segment requested
                MPI_Recv(client_files->files[file_no].file_segments[segment_wanted_pos].hash, HASH_SIZE, MPI_CHAR, send_rq_to, DWD_HASH_TAG, MPI_COMM_WORLD, &status);

                // update client values
                client_files->files[file_no].file_segments[segment_wanted_pos].hash_pos = segment_wanted_pos;
                client_files->files[file_no].available_segments[segment_wanted_pos] = 1;
                client_files->files[file_no].file_segm_no++;

                // when PEER/LEECHER reaches all file segments of a file becomes SEED
                if (segment_wanted_pos == files_info[file_no].total_file_segm_no - 1) {
                    client_files->files[file_no].file_status = SEED;
                    // announce tracker that client finished download of entire file
                    peer_ff_comm(rank, file_no);
                }

                // after downloading first segment, LEECHER becomes PEER
                if (segment_wanted_pos == 0) {
                    client_files->files[file_no].file_status = PEER;
                }

                // increment number of downloads for the file
                downloads_counter[file_no]++;
            }
        }

        // if all files become SEED then announce TRACKER that all files finished download
        if (not_finished_files == 0) {
            peer_fa_comm(rank);
            destroy_files_info(files_info);
            break;
        }
    }

    return NULL;
}

// Function that responds to client and tracker requests
void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;
    MPI_Status status;

    // variable that keeps traffic information of each file
    int traffic_manager[MAX_CLIENTS] = {0};

    while (1) {
        int comm;
        int up_rank_client;
        // receive client rank
        MPI_Recv(&up_rank_client, 1, MPI_INT, MPI_ANY_SOURCE, DWD_RANK_TAG, MPI_COMM_WORLD, &status);
        // receive command
        // (0 - segment request, 1 - all file cleints finished downloads, 2 - file traffic information, 3 - client stop downloading)
        MPI_Recv(&comm, 1, MPI_INT, up_rank_client, UPLOAD_COMM_TAG, MPI_COMM_WORLD, &status);

        if (comm == 1) {
            // exit loop
            break;
        } else {
            int up_file_no;
            // receive file
            MPI_Recv(&up_file_no, 1, MPI_INT, up_rank_client, DWD_FILE_NO_TAG, MPI_COMM_WORLD, &status);

            if (comm == 2) {
                // send total traffic for the file wanted
                int total_traffic = 0;
                for (int i = 1; i < Numtasks; i++) {
                    total_traffic += traffic_manager[i];
                }
                MPI_Send(&total_traffic, 1, MPI_INT, up_rank_client, UPD_TRAFFIC_TAG, MPI_COMM_WORLD);
            } else if (comm == 3) {
                // client stop downloading from this client's file
                traffic_manager[up_rank_client] = 0;
            } else if (comm == 0) {
                // send hash of the segment requested
                int up_segment_no;

                // receive segment wanted
                MPI_Recv(&up_segment_no, 1, MPI_INT, up_rank_client, DWD_SEGMENT_TAG, MPI_COMM_WORLD, &status);

                MPI_Send(client_files->files[up_file_no].file_segments[up_segment_no].hash, HASH_SIZE, MPI_CHAR, up_rank_client, DWD_HASH_TAG, MPI_COMM_WORLD);

                // mark client as using this client file as source
                traffic_manager[up_rank_client] = 1;
            }
        }

    }
    return NULL;
}

// Function that allocates memory for each file swarm
struct File_Swarm *tracker_init_files_swarm(int numtasks) {

    struct File_Swarm *files_swarm = malloc((MAX_FILES + 1) * sizeof(struct File_Swarm));

    for (int i = 1; i <= MAX_FILES; i++) {
        
        files_swarm[i].total_file_segm_no = 0;
        files_swarm[i].file_owners = calloc(numtasks, sizeof(int));
        files_swarm[i].clients_file_data = calloc(numtasks, sizeof(struct File_Data));

        for (int j = 1; j < numtasks; j++) {
            files_swarm[i].clients_file_data[j].available_segments = calloc(MAX_CHUNKS, sizeof(int));
            files_swarm[i].clients_file_data[j].file_segments = calloc(MAX_CHUNKS, sizeof(struct File_Data_Segment));

            for (int k = 0; k < MAX_CHUNKS; k++) {
                files_swarm[i].clients_file_data[j].file_segments[k].hash = malloc((HASH_SIZE + 1) * sizeof(char));
            }
        }
    }

    return files_swarm;
}

// Function that eliberates the memory allocated for structure "File_Swarm"
void destroy_files_swarm(struct File_Swarm *files_swarm) {
    if (files_swarm != NULL) {
        for (int i = 1; i <= MAX_FILES; i++) {
            if (files_swarm[i].file_owners != NULL) {
                free(files_swarm[i].file_owners);
                files_swarm[i].file_owners = NULL;
            }
            if (files_swarm[i].clients_file_data != NULL) {
                for (int j = 1; j < Numtasks; j++) {
                    if (files_swarm[i].clients_file_data[j].available_segments != NULL) {
                        free(files_swarm[i].clients_file_data[j].available_segments);
                        files_swarm[i].clients_file_data[j].available_segments = NULL;
                    }
                    if (files_swarm[i].clients_file_data[j].file_segments != NULL) {
                        for (int k = 0; k < MAX_CHUNKS; k++) {
                            if (files_swarm[i].clients_file_data[j].file_segments[k].hash != NULL) {
                                free(files_swarm[i].clients_file_data[j].file_segments[k].hash);
                                files_swarm[i].clients_file_data[j].file_segments[k].hash = NULL;
                            }
                        }
                        free(files_swarm[i].clients_file_data[j].file_segments);
                        files_swarm[i].clients_file_data[j].file_segments = NULL;
                    }
                }

                free(files_swarm[i].clients_file_data);
                files_swarm[i].clients_file_data = NULL;
            }
        }
        free(files_swarm);
        files_swarm = NULL;
    }
}

// Function that initialize first step made by tracker
void tracker_init_step(int numtasks, struct File_Swarm *files_swarm) {
    MPI_Status status;

    // receive file data from each SEED client
    for (int i = 1; i < numtasks; i++) {
        int files_no = 0;
        MPI_Recv(&files_no, 1, MPI_INT, i, INIT_SEED_NO_TAG, MPI_COMM_WORLD, &status);

        for (int j = 0; j < files_no; j++) {
            int file_no = 0;
            int file_segm_no = 0;

            MPI_Recv(&file_no, 1, MPI_INT, i, INIT_FILE_NO_TAG, MPI_COMM_WORLD, &status);
            MPI_Recv(&file_segm_no, 1, MPI_INT, i, INIT_FILE_SEGMENT_NO_TAG, MPI_COMM_WORLD, &status);

            files_swarm[file_no].total_file_segm_no = file_segm_no;

            files_swarm[file_no].file_owners[i] = 1;

            files_swarm[file_no].clients_file_data[i].file_segm_no = file_segm_no;

            files_swarm[file_no].clients_file_data[i].file_status = SEED;

            for (int k = 0; k < file_segm_no; k++) {
                MPI_Recv(&files_swarm[file_no].clients_file_data[i].file_segments[k].hash_pos, 1, MPI_INT, i, INIT_FILE_SEGMENT_POS_TAG, MPI_COMM_WORLD, &status);

                MPI_Recv(files_swarm[file_no].clients_file_data[i].file_segments[k].hash, HASH_SIZE, MPI_CHAR, i, INIT_FILE_SEGMENT_TAG, MPI_COMM_WORLD, &status);

                files_swarm[file_no].clients_file_data[i].available_segments[k] = 1;
            }
        }
    }
}

// Function that responds to client request
void tracker_rf_comm(struct File_Swarm *files_swarm, int file_no, int cl_rank, int numtasks) {
    // send file owners and the total segments of the file
    MPI_Send(files_swarm[file_no].file_owners, numtasks, MPI_INT, cl_rank, RQ_CLIENTS_TAG, MPI_COMM_WORLD);
    MPI_Send(&files_swarm[file_no].total_file_segm_no, 1, MPI_INT, cl_rank, RQ_SEGMENTS_NO_TAG, MPI_COMM_WORLD);

    // for each file owner, send available segments
    for (int i = 1; i < numtasks; i++) {
        if (files_swarm[file_no].file_owners[i] == 1) {
            MPI_Send(files_swarm[file_no].clients_file_data[i].available_segments, MAX_CHUNKS, MPI_INT, cl_rank, RQ_SEGMENTS_TAG, MPI_COMM_WORLD);
        }
    }
}

// Function that receives updates from client
void tracker_uf_comm(struct File_Swarm *files_swarm, int file_no, int cl_rank, int numtasks) {
    MPI_Status status;
    // receive total segments updated and current status of the file (SEED or PEER)
    MPI_Recv(&files_swarm[file_no].clients_file_data[cl_rank].file_segm_no, 1, MPI_INT, cl_rank, UPD_SEGMENTS_NO_TAG, MPI_COMM_WORLD, &status);
    MPI_Recv(&files_swarm[file_no].clients_file_data[cl_rank].file_status, 1, MPI_INT, cl_rank, UPD_STATUS_TAG, MPI_COMM_WORLD, &status);

    // if the client has more than one segment from the file this means that he ownes a part of it
    if (files_swarm[file_no].clients_file_data[cl_rank].file_segm_no > 0) {
        files_swarm[file_no].file_owners[cl_rank] = 1;
    }

    // receive hash and position of the updated file
    for (int i = files_swarm[file_no].clients_file_data[cl_rank].file_segm_no - MAX_DOWNLOADS; i < files_swarm[file_no].clients_file_data[cl_rank].file_segm_no; i++) {
        MPI_Recv(&files_swarm[file_no].clients_file_data[cl_rank].file_segments[i].hash_pos, 1, MPI_INT, cl_rank, UPD_SEGMENT_POS_TAG, MPI_COMM_WORLD, &status);
        MPI_Recv(files_swarm[file_no].clients_file_data[cl_rank].file_segments[i].hash, HASH_SIZE, MPI_CHAR, cl_rank, UPD_HASH_TAG, MPI_COMM_WORLD, &status);
        
        // mark each segment received as available segment
        files_swarm[file_no].clients_file_data[cl_rank].available_segments[i] = 1;
    }

}

// Function that writes to output file when client announce file download finished
void tracker_ff_comm(struct File_Swarm *files_swarm, int file_no, int cl_rank) {
    char file_name[FILENAME_MAX];
    sprintf(file_name, "client%d_file%d", cl_rank, file_no);

    FILE *fout;
    fout = fopen(file_name, "w+");

    // write all the hashes that client owned for the given file to the output
    for (int i = 0; i < files_swarm[file_no].clients_file_data[cl_rank].file_segm_no; i++) {
        fprintf(fout, "%s\n", files_swarm[file_no].clients_file_data[cl_rank].file_segments[i].hash);
    }

    fclose(fout);

    files_swarm[file_no].clients_file_data[cl_rank].file_status = SEED;
}

void tracker(int numtasks, int rank) {
    struct File_Swarm *files_swarm = tracker_init_files_swarm(numtasks);

    // initialize step - get all initial seeds
    tracker_init_step(numtasks, files_swarm);
    
    // send ack to all clients_file_data
    for (int i = 1; i < numtasks; i++) {
        char ans[4] = "ack\0";
        MPI_Send(ans, 4, MPI_CHAR, i, INIT_ACK_TAG, MPI_COMM_WORLD);
    }

    // counter to monitorize all the clients that finished to download all their files
    int finished_clients = 0;

    while (1) {
        MPI_Status status;
        int cl_rank;


        MPI_Recv(&cl_rank, 1, MPI_INT, MPI_ANY_SOURCE, MSG_RANK_TAG, MPI_COMM_WORLD, &status);
        // Command type:
        //      - (0 - file request, 1 - file update, 2 - file finished, 3 - finished all files)
        int comm;
        MPI_Recv(&comm, 1, MPI_INT, cl_rank, MSG_COMMAND_TAG, MPI_COMM_WORLD, &status);

        if (comm == 0) {
            int file_no;
            MPI_Recv(&file_no, 1, MPI_INT, cl_rank, MSG_FILE_NO_TAG, MPI_COMM_WORLD, &status);

            tracker_rf_comm(files_swarm, file_no, cl_rank, numtasks);
        } else if (comm == 1) {
            int file_no;
            MPI_Recv(&file_no, 1, MPI_INT, cl_rank, MSG_FILE_NO_TAG, MPI_COMM_WORLD, &status);

            tracker_uf_comm(files_swarm, file_no, cl_rank, numtasks);
        } else if (comm == 2) {
            int file_no;
            MPI_Recv(&file_no, 1, MPI_INT, cl_rank, MSG_FILE_NO_TAG, MPI_COMM_WORLD, &status);

            tracker_ff_comm(files_swarm, file_no, cl_rank);
        } else if (comm == 3) {
            finished_clients++;

            // if all clients finished, send command to stop all clients
            if (finished_clients == numtasks - 1) {
                int comm = 1;
                for (int i = 1; i < numtasks; i++) {
                    int tracker_rank = TRACKER_RANK;
                    MPI_Send(&tracker_rank, 1, MPI_INT, i, DWD_RANK_TAG, MPI_COMM_WORLD);
                    MPI_Send(&comm, 1, MPI_INT, i, UPLOAD_COMM_TAG, MPI_COMM_WORLD);
                }
                destroy_files_swarm(files_swarm);
                break;
            }
        }
    }
}

// Function that allocates memory for the each file owned or wanted by the client
struct Client_Files *init_client_files() {
    struct Client_Files *cl_files = malloc(sizeof(struct Client_Files));

    cl_files->files = calloc((MAX_FILES + 1), sizeof(struct File_Data));

    for (int i = 0; i < MAX_FILES + 1; i++) {
        cl_files->files[i].file_segments = malloc(MAX_CHUNKS * sizeof(struct File_Data_Segment));
        cl_files->files[i].available_segments = calloc(MAX_CHUNKS, sizeof(int));
        cl_files->files[i].file_status = UNDEFINED_STATUS;
        cl_files->files[i].file_segm_no = 0;

        for (int j = 0; j < MAX_CHUNKS; j++) {
            cl_files->files[i].file_segments[j].hash = malloc((HASH_SIZE + 1) * sizeof(char));
        }
    }

    return cl_files;
}

// Function that eliberates the memory allocated for structure "Client_Files"
void destroy_client_files(struct Client_Files *cl_files) {
    if (cl_files != NULL) {
        if (cl_files->files != NULL) {
            for (int i = 0; i < MAX_FILES + 1; i++) {
                if (cl_files->files[i].file_segments != NULL) {
                    for (int j = 0; j < MAX_CHUNKS; j++) {
                        if (cl_files->files[i].file_segments[j].hash != NULL) {
                            free(cl_files->files[i].file_segments[j].hash);
                            cl_files->files[i].file_segments[j].hash = NULL;
                        }
                    }
                    free(cl_files->files[i].file_segments);
                    cl_files->files[i].file_segments = NULL;
                }
                if (cl_files->files[i].available_segments != NULL) {
                    free(cl_files->files[i].available_segments);
                    cl_files->files[i].available_segments = NULL;
                }
            }
            free(cl_files->files);
            cl_files->files = NULL;
        }
        free(cl_files);
        cl_files = NULL;
    }
}

// Function used by client to read from file and send information to tracker for the first step
struct Client_Files *peer_read_from_file(FILE* fptr) {

    struct Client_Files *cl_files = init_client_files();

    int seed_files_no;
    fscanf(fptr, "%d", &seed_files_no);

    cl_files->files_no = cl_files->files_no + seed_files_no;

    MPI_Send(&seed_files_no, 1, MPI_INT, TRACKER_RANK, INIT_SEED_NO_TAG, MPI_COMM_WORLD); 

    for (int i = 0; i < seed_files_no; i++) {
        int file_segm_no = 0;
        char file_name[FILENAME_MAX];

        fscanf(fptr, "%s %d", file_name, &file_segm_no);

        int file_no = atoi(&file_name[4]);

        MPI_Send(&file_no, 1, MPI_INT, TRACKER_RANK, INIT_FILE_NO_TAG, MPI_COMM_WORLD); 
        MPI_Send(&file_segm_no, 1, MPI_INT, TRACKER_RANK, INIT_FILE_SEGMENT_NO_TAG, MPI_COMM_WORLD); 

        cl_files->files[file_no].file_status = SEED;
        cl_files->files[file_no].file_segm_no = file_segm_no;

        for (int j = 0; j < file_segm_no; j++) {
            cl_files->files[file_no].file_segments[j].hash_pos = j;
            MPI_Send(&j, 1, MPI_INT, TRACKER_RANK, INIT_FILE_SEGMENT_POS_TAG, MPI_COMM_WORLD);

            fscanf(fptr, "%s", cl_files->files[file_no].file_segments[j].hash);
            MPI_Send(cl_files->files[file_no].file_segments[j].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, INIT_FILE_SEGMENT_TAG, MPI_COMM_WORLD);

            // set true if segment exists
            cl_files->files[file_no].available_segments[j] = 1;
        }
    }

    int peer_files_no;
    fscanf(fptr, "%d", &peer_files_no);

    cl_files->files_no = cl_files->files_no + peer_files_no;

    for (int i = 0; i < peer_files_no; i++) {
        char file_name[FILENAME_MAX];
        fscanf(fptr, "%s", file_name);
        int file_no = atoi(&file_name[4]);

        cl_files->files[file_no].file_status = LEECHER;
    }

    return cl_files;
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *file_status;
    int r;

    // set number of processes as global variable
    Numtasks = numtasks;

    // get file anme
    char in_file[MAX_FILENAME];
    sprintf(in_file, "in%d.txt", rank);

    FILE *fptr = NULL;

    // open file
    fptr = fopen(in_file, "r");

    if (!fptr) {
        printf("Eroare la deschiderea fisierului de initializare\n");
        exit(-1);
    }

    // read from file and send data to tracker
    client_files = peer_read_from_file(fptr);

    r = fclose(fptr);
    if (r) {
        printf("Eroare la inchiderea fisierului de initializare\n");
        exit(-1);
    }

    // wait for ack from the tracker before starting
    char ans[4];
    MPI_Status status;
    MPI_Recv(ans, 4, MPI_CHAR, TRACKER_RANK, INIT_ACK_TAG, MPI_COMM_WORLD, &status);


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

    r = pthread_join(download_thread, &file_status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &file_status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }

    destroy_client_files(client_files);
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

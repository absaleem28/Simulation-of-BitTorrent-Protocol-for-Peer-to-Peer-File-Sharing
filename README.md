TITLE: 

	SIMULATION OF BITTORENT PROTOCOL FOR PEER-TO-PEER FILE SHARING

DESCRIPTION:

    This programme is a simulation for the BitTorrent protocol. In order to implement this protocol, a tracker is needed, which has the role of helping clients to find necessary information about certain files. Clients who participate in this simulation can initially own certain files, and other files they want can be obtained by other clients who initially own them or become owners during the course of the simulation. During the running of the simulation, the client can go through several states with respect to a file. These can be SEED when the client owns the entire file, PEER when the client only owns part of the file, and LEECHER when the client does not own or does not want to share the file with other clients.

SOLUTION:

    Functions:
    
        * CLIENT PERSPECTIVE:
	
            1. void peer():
                Initially, each client must open and read the content of the file (calling the "struct Client_Files *peer_read_from_file()" function) corresponding to its rank. After finishing the initiator step, the client waits confirmation from the TRACKER so that it can start the 2 threads that handle both the download ("void *download_thread_func()") and the response to other clients' requests ("void *upload_thread_func( )").
            
                1.1. struct Client_Files *peer_read_from_file():
                    This function reads values from the given file and based on them fills the fields from the structure "struct Client_Files". This structure is initialized using the function "Client_Files *init_client_files()" which returns a pointer to a zone of memory which the structure have been allocated. This structure holds information as: number of files owned and wanted, and also holds information for each of them. For the initially files owned, the client is set as SEED, but for the files that the client initial wants, it is set as LEECHER (because initially the client doesn't own any segment from the file). In the same time, all the informations about each file owned are being send to the TRACKER.

            2. void *download_thread_func():
                In order to communicate with the TRACKER, each client needs a structure called "struct RQ_File_Info" which it is been allocated using "struct RQ_File_Info *init_files_info()" function.
                After the structure had been allocated, each client checks what files are not complete (the file status is PEER or LEECHER) and for each file the followings steps are being done:
                    - verifies if 10 downloads have been done, if yes => send last updates to the TRACKER, using function "void peer_uf_comm".
                    - request new information about the file from the tracker ("void peer_rf_comm()").
                    - get position of the segmented wanted.
                    - find rank of best client to request from ("int peer_find_client()").
                    - send request for the segment to the client with the rank found.
                    - wait for the requested client to respond with the hash of the segment wanted.
                    - update client data.
                    - check if file status should become SEED => client finished to download this file => call function "void peer_ff_comm()".
                    - check if wanted segment is the first one => file status change from LEECHER to PEER.
                    - increment number of downloads for that file.
                If the client has no more files with status PEER or LEECHER => client finished to download all files wanted => call function "void peer_fa_comm()".
                Functions used by clients to communicates with the TRACKER:
                    2.1. void peer_rf_comm():
                        This function sends to the TRACKER a request for the last wanted file updates. The TRACKER is expected to answer with: integral or partial file owners, segments owned by each client and total number of segments the file should have at the end of the download.
                    2.2. void peer_up_comm():
                        This function sends last updates of a file that reached 10 segments downloaded to the TRACKER. The TRACKER is expected to receive: total number of segments downloaded, status of the file, and the position and the hash of the last 10 segments downloaded.
                    2.3. void peer_ff_com():
                        This function announce the TRACKER that file finished to download all segments. Before announcing the TRACKER, first the client is sending last updates to the TRACKER. Because the updates are being sent every 10 downloads, if the file has a number of segments not multiple of 10, then the last updates are not being seen by the TRACKER.
                    2.4. void peer_fa_comm():
                        This function announe the TRACKER finished to download all the wanted files.
                The algorithm of finding client that ownes the segment wanted can be found in the function "int peer_find_client()":
                    This function iterates through all the clients and checks if the client ownes the segment wanted, if yes, a request is sent in order to find the traffic on that client. Every time a smaller traffic value is found, the request destination is changed. If in the moment when a better traffic value is found, our client is already connected to a peer or seed, it has to send a message to that seed or peer in order to inform that it will no longer download from it. In the end, the peer or seed who ownes the segment and has the lower traffic on it will be choosen to download the segment from it.
                
            3. void *upload_thread_func():
                This function deals with responding to requests from other customers. When sending a request, the client who receives the request expects the following: the rank of the client who sent the request and the command number, and depending on these, he will also expect the following:
                        * command 0 (segment requested): waits number of segment requested, sends back the hash of the segment and increase traffic on the client
                        * command 1 (stop thread running): exits the function (command given by the TRACKER)
                        * command 2 (traffic value requested): sends the value of the traffic at that moment
                        * command 3 (client changed source of download): decrease traffic on the client

    * TRACKER PERSPECTIVE:
    
        1. void tracker():
            Initially, the TRACKER needs a swarm for each file and for this the function "struct File_Swarm * tracker_init_files_swarm()" is called which allocates space for every file swarm. After the space is allocated the function "void tracker_init_step()" is called in order to receive from each client informations of what files they own (SEED) and for each file the TRACKER receive every segment. After the first step is done, the TRACKER sends an "ack" message to each client which means that they can now continue their process of dowloading files.
            After the initial step, the tracker starts to listen requests from the clients and to respond to them. The TRACKER expects the rank and the command from any client and based on those the following are expected:
                * command 0: the TRACKER expects file number, in order to return informations about it by calling function "void tracker_rf_comm()".
                * command 1: the TRACKER expects file number, in order to update its informations by calling function "void tracker_uf_comm()".
                * command 2: the TRACKER expects file number, in order to mark it as done and to write to the output file the segments downloaded by calling the function "void tracker_ff_comm()".
                * command 3: the TRACKER does not expect any, but sends to every client the command to stop running the thread over the function "void *upload_thread_func()" and closes the tracker resulting into ending of the programme.
            Functions used by TRACKER to communicates with the clients:
                1.1. void tracker_rf_comm():
                    This function responds to the client who requested informations about a specific file. The informations consists into: file owners, total number of segments that should be downloaded and the segments owned by each client.
                1.2. void tracker_uf_comm():
                    This function expects some more informations from the client in order to update known ones. The informations expected are: total number of segments downloaded at that time, client' file status and for the last segments downloaded, the TRACKER is expecting their position and hash. Based on the informations received, the TRACKER is updating its values for the spaw of the file.
                1.3. void tracker_ff_comm():
                    This function marks the client' file as SEED and writes to the output file the segments downloaded.


SOLUTION's AUTHOR:

	AL BOURI SALEEM,
	FACULTY OF AUTOMATIC CONTROL AND COMPUTER SCIENCE, UPB

DATA:

	JANUARY, 2024

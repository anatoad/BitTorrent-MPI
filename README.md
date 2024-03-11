APD assignment 

#### About
Distributed program simulating the BitTorrent protocol using MPI.

#### Compiling
for C++ use the **mpic++** compiler<br>
`make build`<br>

#### Running
`mpirun --oversubscribe -np <N> ./main` *with N being the number of processes*<br>

#### Details
Simulation of BitTorrent P2P file sharing using MPI. <br>
Users utilize a BitTorrent **client (peer)** to send/receive files, interacting with a **tracker**. Trackers provide a list of files available for transfer, allowing users to find other users from whom they can download them.<br>
Downloading is faster than HTTP due to lack of a central server limiting bandwidth.<br>

Files are split into segments. Segments are usually downloaded in a random order and are reordered by the client. Interruption of a download causes no data loss and it may be resumed at a later time. This allows the client to download segments that are available at a given time, without having to wait until certain segments become available.<br>

**Client**
- reads the input files available to it
- sends the hash of all segments owned to the tracker, in order
- uses two separate threads for downloading and uploading files
- downloading:
    - send a list of required segments to the tracker
    - request a list of available peers which own the required segments from the tracker
    - for each segment, the client finds a peer in the list provided by the tracker that owns the segment and sends a request to said peer and waits to receive it
- uploading:
    - waits for a message from a peer/tracker
    - if a request is received from a peer, it sends the owned segment
    - if a *FIN* is received from the tracker, the peer ends its execution

**Tracker**
- maintains a list of files' segments and the swarm of peers associated with it
- waits for messages from the clients (and sends information back to the client), with 3 different possible tags:
    - *TAG_REQUEST* - client requests information about available peers
    - *TAG_UPDATE* - client sends update with newly owned segments
    - *TAG_FIN* - client informs tracker that it has received all segments and wants to stop its execution
- after receiving a message with *TAG_FIN* from each peer, the tracker stops its execution


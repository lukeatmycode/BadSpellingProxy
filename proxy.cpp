#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <stdio.h>
#include <string> //string fucntions
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <vector>
#include <ctime>

using namespace std;
const int QUEUELEN = 5;
const int BUFFERSIZE = 1024; //define here for easy tweaking

int listenSock;
int proxyWebSock;
int dataConnection;
int newLength;

string changeHeader(string header){
    int typeStart = header.find("Content-Type:");
    int typeEnd = header.find("\r\n", typeStart+1)+2; //adding two to account for line ends
    string contentType = header.substr(typeStart, typeEnd-typeStart);
    cout << "****" << endl << contentType << endl << "******" << endl;
    string newContent = "Content-Type: text/html\r\n";
    string changed = header.replace(header.find(contentType), contentType.length(), newContent);

    cout << endl << "*******AFTER TYPE SWITCH***********" << endl << changed << endl << "***************" << endl;

    int lenStart = changed.find("Content-Length: ");
    int lenEnd = changed.find("\r\n",lenStart+1) + 2;
    string contentLength = changed.substr(lenStart,lenEnd-lenStart);
    cout << "***" << endl << contentLength << endl << "****" << endl;
    string newCL = "Content-Length: " + to_string(newLength) + "\r\n";
    changed = changed.erase(changed.find(contentLength), contentLength.length());
    changed = changed.append(newCL);

    cout << "*******AFTER LENGTH SWITCH**********" << endl << changed << endl << "****************"<<endl;
    return changed;
}

bool notInBody(int i, string msg) {
    //checks if index i of string msg is inside of a body tag
    size_t start = msg.find("<body>");
    size_t finish = msg.find("</body>");

    if(start == string::npos || finish == string::npos) return false; //there is no body tags, so insert wherever

    if(i > start && i < finish) return false;

    return true;
}

bool notInBold(int i, string msg) {
    //checks if index i of string msg is inside of a bold tag
    size_t start = msg.find("<b>");
    size_t finish = msg.find("</b>");

    if(start == string::npos || finish == string::npos) return false; //there is no body tags, so insert wherever

    if(i > start && i < finish) return false;

    return true;
}

bool notInHeader(int i, string msg) {
    //checks if index i of string msg is inside of a bold tag
    size_t start = msg.find("<h1>");
    size_t finish = msg.find("</h1>");

    if(start == string::npos || finish == string::npos) return false; //there is no body tags, so insert wherever

    if(i > start && i < finish) return false;

    return true;
}

bool isInTag(int i, string msg) {
    //checks if character in msg at index i is inside of HTML tag
    const char * edits = msg.c_str();
    vector<char> tagTracker;
    for(int j = 0; j < msg.length(); j++){
        
        //cout << j << ": " << edits[j] << " tracker is " << tagTracker.size() << endl;
        if(edits[j] == '<') {
            char c = '<';
            tagTracker.push_back(c);
        } 

        if(j==i){
            if(tagTracker.size() != 0) {
                //cout << "Found inside a tag" << endl;
                return true;
            } 
            if(tagTracker.empty() ){
                //cout << "Index "<< j << " " << tagTracker.size() << endl;
                //cout << "Outside tag!" << endl;
                return false;
            }
        } 

        if(edits[j] == '>') {
            tagTracker.pop_back();
        }
        
    }
    return false;
}

string scrambler(string msg, int numErrors){
    //given a msg, it will insert the number of errors specified by numErrors
    //Generate a single random character to swap
    string newMsg = msg;
    srand(time(0)); //seed the random integer generator
    for(int i = 0; i<numErrors;i++){
        
        //swapping index
        int swap = rand() % newMsg.length();
        int ok = isspace(newMsg.at(swap)); //check if the swap is on a whitespace
        //check to see if swap is inside of a tag so you dont mess up HTML formatting
        while(isInTag(swap, newMsg) || ok != 0 || notInBody(swap, newMsg)){
            //cout << swap << " is inside a tag!" << endl;
            swap = rand() % newMsg.length(); //make a new index for swapping
            ok = isspace(newMsg.at(swap));  //check if its a whitespace
        }

        char c;
        c = (rand() % 26 ) + 'a';
        string toInsert;

        if(notInHeader(swap, newMsg) && notInBold(swap, newMsg)) {
            toInsert = "<b></b>"; //bold the mistake
            toInsert.insert(3,1,c); //insert the mistake into tags
        } else {
            toInsert.insert(0, 1, c);
        }
        //cout << "Inserting " << c << " at " << swap << endl;
        newMsg.replace(swap, 1, toInsert);

    }

    newLength = newMsg.length();
    return newMsg;

}

void sendErrorResponse(string msg) {
    string response = "HTTP/1.1 400 Bad Request\r\n";
    response.append("Connection: close\r\n");
    response.append("Server: Spelling Error Proxy\r\n");
    response.append("Content-Type: text/html\r\n\r\n");
    response.append("The proxy only understands HTTP 1.0/1.1 GET requests\r\n");
    response.append(msg + "\r\n");

    vector<char> errorClient(response.begin(), response.end());
    char *sending = &errorClient[0];
    //strcpy(send,received.c_str());  

    //send modified content to client
    int status;
    status = send(dataConnection, sending, strlen(sending), 0);
    if(status == -1) {
        cout << "Error sending error to client :(" <<endl;
    } else {
        cout << "Error sent to client" << endl;
    }
}

void proxyKiller(int signal) {
   //close the listening socket
   close(listenSock);
   close(proxyWebSock);
   close(dataConnection);
   cout<< endl << "You have killed the proxy"<<endl;
   exit(0);
}

string stripProtocol(string url) {
    //cout << "Stripping the protocol from:" << url << endl;
    string http = "http://";
    string https = "https://";
    size_t check = url.find(http);
    if(check != string::npos) {
        url.erase(url.find(http),http.length());
        //cout << "Stripped: " << endl << url << endl;
    }
    check = url.find(https);
    if(check != string::npos) {
        url.erase(url.find(https),https.length());
    }
    return url;
}

int main(int argc, char *argv[]) {
    //string test = "<b>This</b> is my test string I need to test stuff on it</>";
    
    //Catch signal to ensure things close properly
    signal(SIGINT, proxyKiller);

    //Take user input to configure proxy
    int numErr;
    cout << "How many errors should be generated on the modified pages? ";
    cin >> numErr;
    //cout << numErr << " errors will be generated" << endl;
    
    //User can set the port, by default it is 8001
    int listen_port;
    cout << "Which port should I listen on? ";
    cin >> listen_port;
    if(cin.fail()) {
        listen_port = 8001;
    }
    cout << "Listening on port: " << listen_port << endl;

    //Initialize the address of the proxy
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(listen_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    cout << "Address Initialized..." <<endl;
    
    //Create the listening socket 
    listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if(listenSock == -1) {
        cout << "Listening socket creation failed" << endl;
    } else {
        cout << "Listening socket created successfully!" << endl;
    }

    //bind the socket for listening
    int status = bind(listenSock, (struct sockaddr *) &address, sizeof(struct sockaddr_in));
    if(status == -1) {
        cout << "Binding failed" << endl;
        raise(SIGINT);
    } else {
        cout << "Server socket bound successfully!" << endl;
    }

    //listen call
    status = listen(listenSock, QUEUELEN);
    if(status == -1){
        cout << "Listen failed" << endl;
    } else {
        cout << "Begin listening..." << endl;
    }
    
    //Loop forever while listening
    while(true) {
        bool shouldEdit = true; //by default, should try to edit the contents
        //accept a connection 
        dataConnection = accept(listenSock, NULL, NULL);
        if(dataConnection == -1) { 
            cout << "Connection not accepted" << endl;
        }
        //recieve the HTTP GET request from client
        int bytesFromClient;
        char sender[BUFFERSIZE];
        bool flag = false; //wait for all messages
        string request;
        do {
            flag = false;
            bytesFromClient = recv(dataConnection, sender, BUFFERSIZE, 0);
            if(bytesFromClient == -1) {
                cout << "Error receiving message"<< endl;
            } else {
                cout << "Received " << bytesFromClient << " bytes from client" << endl;
                vector<char> buffer(begin(sender), end(sender));
                buffer.resize(bytesFromClient);
                request.append(buffer.begin(), buffer.end());
            }
            size_t cache = request.find("\r\n\r\n"); //check to see if the end of the request is reached
            if(cache == string::npos) flag = true;
        } while(flag); //if the buffer was full, keep checking for more content

        cout << "From client: " << endl << request << endl << "*************End***************" << endl;
        
        
        vector<char> sendMsg(request.begin(), request.end()); //copy the message for sending to web server eventually
        char *sendMessage = &sendMsg[0];
        strcpy(sendMessage,request.c_str()); 
      
        //only want to mess with GET requests
        size_t checkGET = request.find("GET");
        if(checkGET == string::npos) {
            cout << "Not Get" << endl;
            shouldEdit = false;
        }

        //parse it out
        string GET = request.substr(0, request.find("\r\n")); //isolate just the GET request to get the version
        bool isOneOh = false;
        bool isOneOne =  false;
        //Check for versions
        size_t checkVersion1 = GET.find("HTTP/1.0");
        size_t checkVersion11 = GET.find("HTTP/1.1");
        if(checkVersion1 != string::npos){
            isOneOh = true;
        } else if(checkVersion11 != string::npos){
            isOneOne = true;
        } else {
            cout << "Not 1.0/1.1" << endl;
            shouldEdit = false;
            //sendErrorResponse();
            //close(dataConnection); //close the socket for next iteration of loop
            //continue;
        }

        //is there a host field?
        size_t checkHost = request.find("Host: ");
        bool containsHostHeader = false;
        if(checkHost != string::npos) {
            containsHostHeader = true;
        }
        
        //default behaviour
        string url = GET.substr(GET.find(" ")+1, GET.find("\r\n")); //The isolated url
        url = stripProtocol(url); //get rid of http or https in url
        string host = url.substr(0,url.find("/")); //just the host, path begins after the first slash
        string path = url.substr(url.find("/"), url.length());//path includes the first slash, till the end of the URL
        
        //if there is a host header, just use that.
        if(containsHostHeader) {
            host = request.substr(request.find("Host: "));
            host = host.substr(host.find(" ")+1);
            int trim = host.length() - host.find("\r\n");
            host = host.erase(host.find("\r\n"), trim);
        }
        
        //Now, as a client, create an address
        struct hostent *server;
        server = gethostbyname(host.c_str());
        if(server == NULL){
            cout << "Error finding hostname" << endl;
            sendErrorResponse("Error finding hostname");
            close(dataConnection);
            continue;
        } else {
            //cout << "Found the server: " << server->h_name << endl;
        }
        struct sockaddr_in addressServer;
        memset(&address, 0, sizeof(addressServer));
        addressServer.sin_family = AF_INET;
        addressServer.sin_port = htons(80);
        bcopy((char *) server->h_addr, (char *) &addressServer.sin_addr.s_addr, server->h_length);
        cout << "Server Address Initialized..." <<endl;

        //create a client-webserver socket
        proxyWebSock = socket(AF_INET, SOCK_STREAM, 0);
        if(proxyWebSock == -1) {
            cout << "Proxy-webserver socket creation failed" << endl;
        } else {
            //cout << "Proxy-webserver socket created successfully!" << endl;
        }

        //connect to the web server's socket
        int connectionStatus;
        connectionStatus = connect(proxyWebSock,(struct sockaddr *) &addressServer, sizeof(struct sockaddr_in));
        if(connectionStatus == -1) {
            cout << "Unable to connect to webserver" << endl;
        } else {
            //cout << "Connected to web server!" << endl;
        }

        //send the HTTP Request from client to webserver
        //Debug print statements
        /*
        cout << endl << "Sending this message to webserver: " << endl;
        for(int i=0;i!=strlen(sendMessage); i++){
            cout << sendMessage[i];
        }
        cout << endl << "End message" << endl << endl;
        */
        int sendStatus;
        sendStatus = send(proxyWebSock, sendMessage, strlen(sendMessage), 0);
        if(sendStatus == -1) {
            cout << "Sending to webserver failed! :(" <<endl;
        } else {
            //cout << "Request sent to server" << endl;
        }
        //receive the response from web server
        int bytesFromWeb;
        char receiver[BUFFERSIZE];
        string received;
        do {
            bytesFromWeb = recv(proxyWebSock, receiver, BUFFERSIZE, 0);
            if(bytesFromWeb == -1) {
                cout << "Error receiving message"<< endl;
            } else {
                cout << "Received " << bytesFromWeb << " bytes from webserver" << endl;
            }
            vector<char> recBuffer(begin(receiver), end(receiver));
            recBuffer.resize(bytesFromWeb); //discard any junk at the end of the buffer
            received.append(recBuffer.begin(), recBuffer.end());
        } while(bytesFromWeb == BUFFERSIZE); //if the buffer was full, keep checking for more content
       
        //should have the response, debug print out to make sure
        //cout << endl << "From webserver: " << endl << received <<endl<<endl;
         
        //close the connection with webserver, done with it
        close(proxyWebSock);

        //Need to parse the output from the webserver, we only want to change text/plain or text/html, leave other content alone
        string header;
        string page;
        header = received.substr(0, received.find("\r\n\r\n")); //isolates the header
        //complicated line of code below, finds the line where content type is specified
        string contentType = header.substr(header.find("Content-Type:"), header.find("\r\n", header.find("Content-Type:")));
        size_t textPlain = contentType.find("text/plain");
        size_t textHTML = contentType.find("text/html");

        if(textPlain == string::npos && textHTML == string::npos) {
            shouldEdit = false; //dont mess with page if it isn't html or text
        }
        cout << "*******RECEIVED HEADER RESPONSE HEADER ******* " << endl << header << endl << "******END******" << endl;

        
        if(shouldEdit) {
            //cout << endl << "Changing" << endl;
            page = received.substr(received.find("\r\n\r\n"));
            page.replace(page.find("\r\n\r\n"), strlen("\r\n\r\n"), ""); //the page contents for modification
            page = scrambler(page,numErr); //scramble the contents 
            
            header = changeHeader(header); //The header needs to be changed to Content-type: text/html to allow for bolding;

            received = header + "\r\n\r\n" + page; //recombine the header and content
        }
        vector<char> sendClient(received.begin(), received.end());
        char *sendFinal = &sendClient[0];
        strcpy(sendFinal,received.c_str());  
        
        cout << endl << "**********Sending this message to client*************** " << endl;
        for(int i=0;i!=strlen(sendFinal); i++){
            cout << sendFinal[i];
        }
        cout << endl << "********End message********" << endl; 
        

        //send modified content to client
        int deliver;
        deliver = send(dataConnection, sendFinal, strlen(sendFinal), 0);
        if(deliver == -1) {
            cout << "Sending to client failed! :(" <<endl;
        } else {
            cout << "*********Content sent to client**********" << endl;
        }

        //close connection with client
        close(dataConnection);
    }

    
    return 0;
}

#include <arpa/inet.h>
#include <assert.h>
#include <cstdio>
#include <curl/curl.h>
#include <ifaddrs.h>
#include <iostream>
#include <iterator>
#include <netdb.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/socket.h>
#include <time.h>
#include <typeinfo>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Poco/Base64Decoder.h"
#include "Poco/Base64Encoder.h"
#include "Poco/Exception.h"
#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/Net/NetException.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Net/WebSocket.h"
#include "Poco/Path.h"
#include "Poco/StringTokenizer.h"
#include "Poco/URI.h"
#include "Poco/Util/IniFileConfiguration.h"
#include "Poco/Util/XMLConfiguration.h"
#include "rapidjson/document.h"

#include <netinet/in.h>

extern "C"
{
#include <pthread.h>
}

using Poco::AutoPtr;
using Poco::Base64Decoder;
using Poco::Base64Encoder;
using Poco::Exception;
using Poco::Net::HTTPClientSession;
using Poco::Net::HTTPRequest;
using Poco::Net::HTTPResponse;
using Poco::Net::HTTPServerRequest;
using Poco::Net::HTTPServerResponse;
using Poco::Net::WebSocket;
using Poco::Net::WebSocketException;
using Poco::Util::IniFileConfiguration;
using Poco::Util::XMLConfiguration;
using Poco::replace;
using Poco::replaceInPlace;

using namespace Poco::Net;
using namespace Poco;
using namespace std;

// function prototypes
string toBase64 (const string &source);
string fromBase64 (const string &source);
void message_action(string rest_url, string message);
void SendMessageToMain(const void *message);
string urlencode(const string &s);
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
void MessageReceivedFromMain(string MessageReceived);
char *getMACC();
string curl_get(const std::string& url, std::ostream& os, long timeout = 30);

string GetRestCall(string rest_url, string data);
// !function prototypes



//SITE VARIABLES
#define MAX_BUF 2048
string ApiEndpointUrl = "http://dev.Domain.com/api/io/";

const char * PiSocketFifoFile = "/tmp/pisocketfifo";
void * CreatePiSocketFifo(void * argument);  //create the actual file and make socket available
struct stat getfifostat;

//Fifo data from piSocket to Main
const char * MainFifoFile = "/tmp/mainfifo";



struct MemoryStruct {
	char *memory;
	size_t size;
};



char buf[1024];

int main()
{
	AutoPtr<XMLConfiguration> pConf(new XMLConfiguration("settings.xml")); // instantiating the XMLConfiguration and reading from setting.xml

	char buffer[2048];
	memset(buffer, 0, sizeof buffer);
	int flags;
	int n;
	string payload;
	string out;
	int msg_cnt = 0;

	pthread_t fifoReadThread;	//indivudual thread for fifo so it doesn't block other stuff
	int iret1 = pthread_create(&fifoReadThread, NULL, CreatePiSocketFifo, NULL);




	//variables to be set in xml config file //
	string signalr_service_url = pConf->getString("signalr_url"); 
	string signalr_url_endpoint = pConf->getString("signalr_url_endpoint");
	int signalr_service_port = pConf->getInt("signalr_port");
	int timeout_seconds = pConf->getInt("timeout");
	string api_endpoint_url = pConf->getString("api_endpoint_url"); // ""
	//pipe_from_main = pConf->getString("pipe_from_main");
	//string pipe_from_socket = pConf->getString("pipe_from_socket");

	//--------------------------------------//

	//cout << endl << "=============================================================" << endl;
	//cout << "api_endpoint_url -> " << api_endpoint_url << endl;
	//cout << "signalr_url_endpoint -> " << signalr_url_endpoint << endl;
	//cout << "signalr_service_port -> " << signalr_service_port << endl;
	//cout << "timeout_seconds -> " << timeout_seconds << endl;
	//cout << "api_endpoint_url -> " << api_endpoint_url << endl;
	//cout << "pipe_from_main -> " << pipe_from_main;
	//cout << endl << "=============================================================" << endl << endl;

	//cout << "Opening pipe: " << pipe_from_main << endl;

	//fd = open(pipe_from_main.c_str(), O_WRONLY); // open pipe as readonly
	//write(fd, "Hello World", sizeof("Hello World"));
	//close(fd);

conn:	//label for the goto's in the catches 
	try{
		time_t seconds_past_epoch = time(0);
		cout << "STARTING " << endl;

		char port[100];
		char id[100];
		snprintf(port, sizeof(port), "%d", signalr_service_port); // converting int variables to char[]
		snprintf(id, sizeof(id), "%ld", (seconds_past_epoch * 1000)); // converting int variables to char[]


		string my_mac = getMACC(); //Get the mac address

		//compose the URI for getting the token
		URI uri("http://" + signalr_service_url + ":" + port + "/" + signalr_url_endpoint + "/negotiate?_" + id + "&UID=" + my_mac);
		HTTPClientSession session(uri.getHost(), uri.getPort()); // instantiating a client session
		//if there is a network problem between Pi and SignalR everything will break and the catch will point back to the goto label
		//we set this in order to a lower minimise the retry period thus the downtime 
		session.setTimeout(timeout_seconds * 1e+6); // time in microseconds; 
		string path(uri.getPathAndQuery());

		// send the request
		HTTPRequest req(HTTPRequest::HTTP_GET, path, HTTPMessage::HTTP_1_1);
		session.sendRequest(req);


		StringTokenizer tokenizer(session.socket().address().toString(), ":", StringTokenizer::TOK_TRIM); // get the request originating address:ip from the session socket initiated by HTPP; tokenize it for IP extraction
		//string my_mac = getMAC((tokenizer[0]).c_str()); // call IP to MAC converter

		


		// get response
		HTTPResponse res;
		istream& is = session.receiveResponse(res); // stream the request
		cout << res.getStatus() << " " << res.getReason() << endl; // get the status code of the transaction

		// convert the istream to sting for further processing
		istreambuf_iterator<char> eos;
		string s(istreambuf_iterator<char>(is), eos);

		const char * cc = s.c_str();
		// instantiate a rapidjson document and fill it up with the response of the negotiation request
		rapidjson::Document document; 
		document.Parse<0>(cc); 
		string token = document["ConnectionToken"].GetString(); // parse the response and get the connectionToken

		//=============================================

		//connect to signarR using the connectionToken got previously
		HTTPClientSession cs(signalr_service_url, signalr_service_port); // instantiate simple webclient
		string what = "/" + signalr_url_endpoint + "/connect?transport=webSockets&connectionToken=" + urlencode(token) + "&UID=" + my_mac + "&connectionData=%5B%7B%22name%22%3A%22myhub%22%7D%5D&tid=10"; // compose the request string
		HTTPRequest request(HTTPRequest::HTTP_GET, what, "HTTP/1.1"); // the protocol MUST be HTTP/1.1, as described in RFC6455; else the UPGRADE to websocket request will fail
		request.set("Host", signalr_service_url); // specify the Host header to be sent
		HTTPResponse response; // instantiate a http response
		cout << response.getStatus() << " " << response.getReason() << endl;
		WebSocket * ws = new WebSocket(cs, request, response); // instantiate a WebSocket transaction to the 'cs' session, sending the 'request' and storing the 'response'

		//sample of a message to be sent
		payload = "{\"H\":\"myhub\",\"M\":\"Send\",\"A\":[\"" + my_mac + "\",\"invoked from " + replaceInPlace(my_mac, std::string(":"), std::string("-")) + " client\"],\"I\":0}";
		// cout << endl << payload << endl ;
		ws->sendFrame(payload.data(), payload.size(), WebSocket::FRAME_TEXT); // send the message to signalR using the payload and setting the type of frame to be sent as FRAME_TEXT

		flags = 1;
		// starting the receiving loop
		while( (flags & WebSocket::FRAME_OP_BITMASK) != WebSocket::FRAME_OP_CLOSE ) // while websocket session open
		{
			n = ws->receiveFrame(buffer, sizeof(buffer), flags); // n is the received frame
			// flags store the response flags of the frame
			// 129 = single frame response
			// 1   = start of multi-frame response
			// 0   = continuation frame of a multi-frame response
			// 128 = FIN frame os a multi-frame response

			// signalR send data in JSON format, and it send empty messages (empty json document) on a regular basis

			if( (n != 2) && flags !=1 ) // filter out empty jsons and multiframe responses (multiframe will be treated further on)
			{
				cout << "RCV[" << msg_cnt << "]=>	" << buffer << " ===== " << unsigned(flags) << endl;
			}

			if(flags == 1){ // if I get a start frame of a multi-frame response means that I am getting something usefull from signalR
				string str(buffer);
				out += str;
				// due to flag == 1, we are expecting several frames, until we got flag == 128
				do{
					n = ws->receiveFrame(buffer, sizeof(buffer), flags);
					string str(buffer);
					out += str; // we add the next frame/frames to the out variable, to construct the whole JSON message
					str = "";
					memset(buffer, 0, sizeof buffer); // be sure to empty the buffer to don't end up with junk
				}while(flags != 128);
				cout << endl << "=> " << out << endl;

				//not as we got a valid response from signalR endpoint, lets process it

				//convert the out variable and pass is as a Document to the JSON parser
				const char * c = out.c_str();
				rapidjson::Document document;
				document.Parse<0>(c);
				out = document["M"][rapidjson::SizeType(0)]["A"][rapidjson::SizeType(1)].GetString(); // get out only the actual sent message from the response message


				SendMessageToMain(out.c_str());


				cout << "Msg Received";

				message_action(api_endpoint_url,out); // do something with the message in the message_action function
				out = "";
			}
			msg_cnt++;
			memset(buffer, 0, sizeof buffer); // we always cleanup
		}
		ws->shutdown();
	}
	// if something goes wrong with the connection, we try to recover
	catch (WebSocketException& exc0)
	{
		cout <<"WebSocketException "<< exc0.displayText() << endl;
	}
	catch (Poco::TimeoutException& exc1) // handle webclient errors 
	{
		goto conn; // lets try again from the top
		cout <<"TimeoutException "<< exc1.displayText() << endl;
		// return 1;
	}
	catch (ConnectionResetException& exc) // handle connec errors
	{
		goto conn; // lets try again from the top
		cout << "!!!ConnectionResetException:" << exc.displayText() << endl;
		// return 1;
	}
	cout << strerror(errno) << endl;
	return 0;
}


// callback function for curl
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	((string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

// PUT the received message on the API URL
void message_action(string rest_url, string message)
{
	CURL* curl; // instantiate CURL
	string readBuffer;
	string url = rest_url + urlencode(message); // compose the PUT url
	readBuffer.clear(); // make sure the buffer is empty

	curl_global_init(CURL_GLOBAL_ALL);  // initiate the CURL
	curl = curl_easy_init(); // initiate the CURL
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str()); // set the URL
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback); //this gets called by libcurl as soon as there is data received that needs to be saved
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer); // data pointer to pass to the write function. If you use the CURLOPT_WRITEFUNCTION option, this is the pointer you'll get as input.
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); // set curl to use PUT
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ""); // we don't send anything, as the payload is in the URL itself
	curl_easy_perform(curl); // do the actual CURL

	curl_easy_cleanup(curl); // cleanup
	curl_global_cleanup(); // cleanup
	cout << "PUT resp: " << readBuffer << endl;

}







void split(vector<string> &tokens, const string &text, string sep) {
	int start = 0, end = 0;
	while ((end = text.find(sep, start)) != string::npos) {
		tokens.push_back(text.substr(start, end - start ));
		start = end + 3;
	}
	tokens.push_back(text.substr(start));
}

string toBase64(const string &source)
{
	istringstream in(source);
	ostringstream out;
	Poco::Base64Encoder b64out(out);

	copy(istreambuf_iterator<char>(in),
		istreambuf_iterator<char>(),
		ostreambuf_iterator<char>(b64out));
	b64out.close(); // always call this at the end!

	return out.str();
}

string fromBase64(const string &source)
{
	istringstream in(source);
	ostringstream out;
	Poco::Base64Decoder b64in(in);

	copy(istreambuf_iterator<char>(b64in),
		istreambuf_iterator<char>(),
		ostreambuf_iterator<char>(out));

	return out.str();
}

string urlencode(const string &s)
{
	//RFC 3986 section 2.3 Unreserved Characters (January 2005)
	const string unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";

	string escaped="";
	for(size_t i=0; i<s.length(); i++)
	{
		if (unreserved.find_first_of(s[i]) != string::npos)
		{
			escaped.push_back(s[i]);
		}
		else
		{
			escaped.append("%");
			char buf[3];
			sprintf(buf, "%.2X", s[i]);
			escaped.append(buf);
		}
	}
	return escaped;
}

char *getMACC(){
	struct ifreq ifr;
	struct ifconf ifc;
	char buf[1024];
	int success = 0;

	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock == -1) { /* handle error*/ };

	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) { /* handle error */ }

	struct ifreq* it = ifc.ifc_req;
	const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));

	for (; it != end; ++it) {
		strcpy(ifr.ifr_name, it->ifr_name);
		if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
			if (!(ifr.ifr_flags & IFF_LOOPBACK)) { // don't count loopback
				if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
					success = 1;
					break;
				}
			}
		}
		else { /* handle error */ }
	}

	unsigned char mac_address[6];

	if (success)
		memcpy(mac_address, ifr.ifr_hwaddr.sa_data, 6);

	//printf("mac_address : %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n", mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);

	char *mac_addr = (char*)calloc(sizeof(char), 18);
	sprintf(mac_addr, "%02x:%02x:%02x:%02x:%02x:%02x", mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);

	return mac_addr;
}


void SendMessageToMain(const void *message){
	struct stat st;
	int a = stat(MainFifoFile, &st);


	cout << strerror(errno) << endl;

	int fd = open(MainFifoFile, O_WRONLY);
	write(fd, message, 100);
	close(fd);
}



//constantly running looking for new pipe fifo info in its own thread for data received from MainPi
void * CreatePiSocketFifo(void * argument){  
	if (stat(PiSocketFifoFile, &getfifostat) != 0)  //make sure exists before trying to connect to it
		mkfifo(PiSocketFifoFile, 0666);  //make actual fifo file



	char buf[MAX_BUF] = "";
	int roPipipe = open(PiSocketFifoFile, O_RDONLY);  //Open pipe as readonly

	while (1){

		/* create the FIFO (named pipe) */
		if (read(roPipipe, &buf, MAX_BUF) > 0)  // if there is data on the pipe we reed it in buf
		{
			printf("FIFO Received: %s\n", buf); // do something with the recieved data -> TODO: parse it and send it over to slaves

			MessageReceivedFromMain(buf);
		}
		sleep(5);
	}

	close(roPipipe);
}

void MessageReceivedFromMain(string MessageReceived ){
	vector<string> message; //will hold all the pieces of a message received from MainPi

	split(message, MessageReceived, "|~|");  //Put,Post,Get |~| FunctionName |~| Data (optional)
	string verb = message[0];
	string url = ApiEndpointUrl + message[1];
	string data = "";
	/*if (message.size == 2){
		data = message[2];
	}*/

	if (verb == "PUT"){

	}
	if (verb == "GET"){
		string input = curl_get(url, std::cout);  //call server and get return to pass to MainPi. 
		SendMessageToMain(input.c_str());

		//GetRestCall(url, data);
	}



	
	
}


/* There might be a realloc() out there that doesn't like reallocing NULL pointers, so we take care of it here */
void *myrealloc(void *ptr, size_t size)
{
	if (ptr)
		return realloc(ptr, size);
	else
		return malloc(size);
}

/// Handles reception of the data from curl
/// \param ptr pointer to the incoming data
/// \param size size of the data member
/// \param nmemb number of data memebers
/// \param stream pointer to I/O buffer
/// \return number of bytes processed
static size_t write_to_string(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;
	mem->memory = (char *)myrealloc(mem->memory, mem->size + realsize + 1);
	//mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL) {
		/* out of memory! */
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}

string curl_get(const std::string& url, std::ostream& os, long timeout)
{
	curl_global_init(CURL_GLOBAL_ALL);

	CURLcode code(CURLE_FAILED_INIT);
	CURL* curl = curl_easy_init();

	struct MemoryStruct chunk;  //memory area to return string from web call
	chunk.memory = NULL; /* we expect realloc(NULL, size) to work */
	chunk.size = 0;    /* no data at this point */

	if (curl)
	{
		if (CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_to_string))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_FILE, &os))
			//&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk))

			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_URL, url.c_str())))
		{
			code = curl_easy_perform(curl);
		}
		curl_easy_cleanup(curl);
	}

	std::string ReturnMessageFromServer = chunk.memory;

	curl_global_cleanup();

	return ReturnMessageFromServer;
}








string GetRestCallOld(string rest_url){
	//TestRead();

	CURL* curl; // instantiate CURL
	string readBuffer;
	string url = rest_url; // compose the url
	readBuffer.clear(); // make sure the buffer is empty

	cout << url << endl;

	CURLcode res;

	curl_global_init(CURL_GLOBAL_ALL);  // initiate the CURL
	curl = curl_easy_init(); // initiate the CURL
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str()); // set the URL
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback); //this gets called by libcurl as soon as there is data received that needs to be saved
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer); // data pointer to pass to the write function. If you use the CURLOPT_WRITEFUNCTION option, this is the pointer you'll get as input.
	//curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "Get"); // set curl to use Get
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ""); // we don't send anything, as the payload is in the URL itself

	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, true);

	res = curl_easy_perform(curl); // do the actual CURL

	if (res != CURLE_OK)
		fprintf(stderr, "curl_easy_perform() failed: %s\n",
		curl_easy_strerror(res));


	cout << endl << WriteCallback << endl;

	curl_easy_cleanup(curl); // cleanup
	curl_global_cleanup(); // cleanup
	cout << "PUT resp: " << readBuffer << endl;

	return readBuffer;
}
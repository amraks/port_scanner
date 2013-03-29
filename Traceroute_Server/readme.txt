/*==================================================================================

README for Traceroute Server - Porject 1

Author: Harsh Savla

Description: This is a client-server application. The server is capable of executing 
traceroute commands issued by the client. The server supports logging, multi-threading 
so it can handle multiple clients, rate limiting and time-outs which times out an 
inactive client.
====================================================================================*/

1. To compile the server, use:	make

2. To run the server use: ./sv <option1> <value1> <option2> <value2>....

3. For help: ./sv -h or ./sv --help

4. To start the server on particular hostname and port use: ./sv <hostname> <port_number>
	eg. ./sv localhost 8888

5. The project folder contains 'file1.txt' which contains a list of 'traceroute  <dest>' commands. The client can issue 'traceroute file1.txt'.

Important files:
1. tracerouteServer.cpp
2. Makefile
3. readme.txt
4. references.txt

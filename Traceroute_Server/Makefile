all:
	g++ tracerouteServer.cpp -o sv -std=c++0x -pthread

client: client.cpp
	g++ client.cpp -o client -std=c++0x -pthread

clean:
	rm sv logfile.txt

cf:
	echo > file1.txt
	echo > file2.txt
	echo > file3.txt
	echo > file4.txt

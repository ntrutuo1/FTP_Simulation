# FTP_Simulation
A simulated FTP protocol is built in only C++ language. It can be connected with third-party apps like FileZilla. However you must run SERVER file as admin.

To run this and connect with third-party app.
Change Directory to folder SERVER

Run:

**g++ server.cpp -o server**

**sudo ./server**

After that you open **FileZilla or another app** and login with  accounts in **users.txt.**

**NOTE:**
After cloned into your PC, go to server.cpp and change** serverIP to your PC IP**, by default i set it to **127.0.0.1**, default PORT is 8888.

Server file is designed to intect upload, download, change directory, list, and login in passive mode (FileZilla).

On Client side, it can interact with server through command line:
After compiled:

You can use USER, PASS, CWD, PWD, LIST, STOR, RETR to interact with server.



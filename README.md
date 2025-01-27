# FTP_Simulation
A simulated FTP protocol is built in only C++ language. It can be connected with third-party apps like FileZilla. However you must run SERVER file as admin.

To run this and connect with third-party app.
Change Directory to folder SERVER

Run:
g++ server.cpp -o server 
sudo ./server

After that you open FileZilla or another app and login with  accounts in users.txt.

NOTE:
After cloned into your PC, go to server.cpp and change serverIP to your PC IP, by default i set it to 127.0.0.1

Do the same with Client.


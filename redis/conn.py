import socket

class ErrorMassage(Exception) :
	pass

class 

class RedisConnection :
	
	def __init__(self,hostname='localhost',port=6379) :
		self._date = (hostname,port)
		self._sock = socket.socket()
		self._sock.connect(self._date)
		#FIXME
		self.TERM = '\r\n'
		
	def _prepareCommands(self, args) :
		buf = '*';
		buf += str(len(args))
		buf += self.TERM;
		for x in args  :
			buf += '$'
			buf += str(len(str(x)))
			buf += self.TERM
			buf += str(x)
			buf += self.TERM
		
		self._sock.send(buf.encode())
			
			
	def __del__(self) :
		self._sock.close();

conn = RedisConnection()
conn._prepareCommands(['SET','mykey',"myvalue"])


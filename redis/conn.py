import socket

class RedisError(Exception) :
	pass


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
	
	def _parseSingle(self) :
		tmp = self._sock.recv(4096).encode()
		return tmp.ljust(find("\n"))
			
	def _parseResponse(self) :
		first = self._sock.recv(1)
		if(first == b'+')
			return self._parseSingle()
			
	def __del__(self) :
		self._sock.close();

conn = RedisConnection()
conn._prepareCommands(['SET','mykey',"myvalue"])


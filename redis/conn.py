import socket

class RedisError(Exception) :
	pass

class NoKeyException(Exception) :
	pass

def buildMessage(args) :
	buf = ['*'+str(len(args))]
	for x in args  :
		buf.append( '$'+str(len(str(x))) )
		buf.append( str(x) )
	return "\r\n".join(buf) + "\r\n"
	
#Można obejść przez użycie makefile z sockets
def readMessage(sock) :
	message = ""
	term = b"\r"
	while(True) :
		tmp = sock.recv(1)
		if tmp == term :
			break;
		message+=tmp.decode()
	sock.recv(1)
	return message

class RedisConnection :
	
	def __init__(self,hostname='localhost',port=6379) :
		self._date = (hostname,port)
		self._sock = socket.socket()
		self._sock.connect(self._date)
	
	
	def _parseSingle(self) :
		return readMessage(self._sock)
		
	def _parseError(self) :
		raise RedisError(readMessage(self._sock))
	
	def _parseInteger(self) :
		return int(readMessage(self._sock))
		
	def _parseBulk(self) :
		size = int(readMessage(self._sock))
		if size < 0 :
			return None
		return readMessage(self._sock)
		
	def _parseMultiBulk(self) :
		size = int(readMessage(self._sock))
		ans = []
		if size < 0 :
			return None
		for x in range(size) :
			ans.append(_parseBulk(self._sock))
		return ans
			
	def _parseResponse(self) :
		
		first = self._sock.recv(1)
		if first == b'+' :
			return self._parseSingle()
		if first == b'-' :
			return self._parseError()
		if first == b':' :
			return self._parseInteger()
		if first == b'$' :
			return self._parseBulk()
			
	def execute(self,args) :
		msg = buildMessage(args)
		print (msg.encode())
		self._sock.send(msg.encode())
		return self._parseResponse()
		#print (tmp)
			
	def __del__(self) :
		self._sock.close();
		
	def get(self,key) :
		return self.execute(["GET",key])
	
	def expire(self,key,time) :
		return self.execute(["EXPIRE",key,time])
	
	def delete(self,key) :
		return self.execute(["DEL",key])
	
	def set(self,key,value) :
		return self.execute(["SET",key,value])

test = RedisConnection()
print (test.set("a","myvalue"),
test.expire("a",255))
test.execute(["dupa"])

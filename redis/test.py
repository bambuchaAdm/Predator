import socket

class RedisMock(socket.socket) :
	def __init__(self) :
		super().__init__()
		self.bind(('localhost',6379))
		self.listen(2)
	
	def __del__(self) :
		self.close()

test = RedisMock()
while(True) :
	con, ad = test.accept()
	buf = con.recv(1024)
	print (buf)
	print (buf.decode())
	#
	con.close()


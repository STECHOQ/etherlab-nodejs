const net = require('net');
const fs = require('fs');
const EventEmitter = require('events');

class __server extends EventEmitter {
	constructor(_path, port = 0, toJSON = true, debug = false){
		super();

		this._isTCP = net.isIP(_path) ? true : false;
		this._path = _path;
		this._port = this._isTCP ? port : undefined;
		this._clients = {};
		this._aliases = {};
		this._debug = debug;
		this._toJSON = toJSON;

		this._reattemptInterval = 500;
	}

	/**
	 *	emit data if only there is at least 1 listener
	 *	@private
	 *	@param {string} eventName - event name to be emitted
	 *	@param {*} values - event data, any datatype can be emitted
	 * */
	_emit(eventName, ...values){
		const self = this;

		if(self.listenerCount(eventName) > 0){
			self.emit(eventName, ...values);
		}
	}

	/**
	 *	Create TCP or UNIX Domain socket server
	 * */
	create(){
		const self = this;

		if(self._path == undefined){
			throw 'Socket Path is undefined';
		}

		self.server = net.createServer(function(socket) {
			socket.pipe(socket);
		});

		if(self._isTCP){
			self.server.listen(self._port, self._path);
		} else {
			if(fs.existsSync(self._path)){
				fs.unlinkSync(self._path);
			}

			self.server.listen(self._path);
		}

		self.server.on('listening', function() {
			self._emit('listening');

			if(self._debug){
				console.log('Server : Listening to', self.server.address());
			}
		});

		self.server.on('connection', function(socket) {
			if(self._isTCP){
				//for TCP connecntion, use remote IP and port as id
				const clientId = `${socket.remoteAddress}:${socket.remotePort}`;

				self._clients[clientId] = socket;
				socket.id = clientId;
			} else {
				// generate random number as client id
				const randomId = Math.round(Math.random() * (10 ** 10));
				socket.id = randomId;
				// add to connected clients
				self._clients[randomId] = socket;
				// send generated id to client
				socket.write(JSON.stringify({
						code : 1,
						id : randomId,
					})
				);
			}

			if(self._debug){
				console.log('Connection established.', self._isTCP ? `From ${socket.remoteAddress}:${socket.remotePort}` : ``);
			}

			socket.on('data', function(chunk) {
				//always emit raw data
				self._emit('raw', chunk);

				if(!self._toJSON){
					return;
				}

				const data = chunk.toString().replace(/[^\x20-\x7E]+/g, '').trim();

				try{
					const obj = JSON.parse(data);

					self._emit('data', obj);

					// reserved {code : 1}, as a message to acknowledge between server and clients
					if(+obj.code === 1){
						if(obj.alias != null){
							self._aliases[obj.alias] = `${socket.remoteAddress}:${socket.remotePort}`;
						}
					}
				} catch(error) {
					console.error('JSON Error', error);
					self._emit('data', data);
				}
			});

			socket.on('end', function() {
				self._emit('end', socket.id);

				delete self._clients[socket.id];

				if(self._debug){
					console.log('Closing connection with the client ', socket.remoteAddress ?? self._isTCP);
				}
			});

			socket.on('error', function(err) {
				self._emit('error', err);

				if(self._debug){
					console.error(socket.id,':',err);
				}

				if (err.code === 'ECONNRESET' || err.code === 'ECONNREFUSED') {
					self._deleteClient(socket.id);
				}
			});
		});

		self.server.on('error', function(err) {
			self._emit('error', err);

			if (err.code === 'EADDRINUSE') {
				if(self._debug){
					console.error('Address in use, retrying...');
				}

				setTimeout(() => {
					self.server.close();

					if(self._isTCP){
						self.server.listen(self._port, self._host);
					} else {
						fs.unlinkSync(self._path);
						self.server.listen(self._path);
					}
				}, self._reattemptInterval);
			}
		});
	};

	/**
	 * Delete client with particular id from list clients
	 * */
	_deleteClient(id){
		const self = this;

		self._clients[id].end();
		self._clients[id].destroy();
		delete self._clients[id];

		for(const key in self._aliases){
			if(self._aliases[key] === id){
				delete self._aliases[key];
				break;
			}
		}
	};

	/**
	 * Write data to one of connected clients
	 * @param {string} dest - can be client's alias or IP:port/unique client id
	 * @returns {boolean} write status
	 * */
	send(msg, dest){
		const self = this;

		if(!dest){
			return self.sendAll(msg);
		}

		if(self._aliases[dest]){
			dest = self._aliases[dest];
		}

		// if client is not found, return immediately
		if(!self._clients[dest]){
			return false;
		}

		return self._clients[dest].write(msg instanceof Object ? JSON.stringify(msg) : msg);
	};

	sendAll(msg){
		const self = this;

		let status = true;
		for(const dest in self._clients){
			status &= self._clients[dest].write(msg instanceof Object ? JSON.stringify(msg) : msg);
		}

		return status;
	};

	/**
	 * Returns list of connected clients
	 * @returns {Object[]} connected clients
	 * */
	getConnectedClients(){
		const self = this;
		const list = [];
		const _aliases = {};

		for(const key in self._aliases){
			_aliases[self._aliases[key]] = key;
		}

		for(const key in self._clients){
			if(_aliases[key]){
				list.push({id:key, alias:_aliases[key]});
			} else {
				list.push({id:key});
			}
		}

		return list;
	};
}

module.exports = __server;

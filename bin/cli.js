#!/usr/bin/env node

const fs = require('fs');

const __configPath = `${__dirname}/config/conf.json`;

const __ETHERLAB = require(`${__dirname}/../`);
const __SOCKET = require(`${__dirname}/socketServer.class.js`);

let configs = {};

function __CONFIG_TEMPLATE__(){
	this.path = null;
	this.port = null;
	this.socket = undefined;

	this.etherlab = undefined;
	this.slaves = null;

	this.interval = 0;
	this.onData = false;
	this.received = {};
	this.frequency = 1000;

	this.autostart = true;
}

function configFileWrite(){
	const _tmp = {
		socket: {
			path: configs.path,
			port: configs.port,
		},
		slaves: configs.slaves,
		frequency: configs.frequency,
		interval: configs.interval,
		autostart: configs.autostart,
	};

	return fs.writeFileSync(__configPath, JSON.stringify(_tmp));
}

function configFileRead(){
	if(fs.existsSync(__configPath)){
		const _tmp = require(__configPath);
		const _cfg = new __CONFIG_TEMPLATE__();

		if(_tmp.socket && _tmp.socket.path != null){
			_cfg.path = _tmp.socket.path;
		}

		if(_tmp.socket && _tmp.socket.port != null){
			_cfg.port = _tmp.socket.port;
		}

		if(_tmp.slaves != null){
			_cfg.slaves = _tmp.slaves;
		}

		if(_tmp.frequency != null){
			_cfg.frequency = _tmp.frequency;
		}

		if(_tmp.interval != null){
			_cfg.interval = _tmp.interval;
		}

		if(_tmp.autostart != null){
			_cfg.autostart = _tmp.autostart;
		}

		return _cfg;
	}

	// config file doesnt exist, create new one
	const _tmp = {
		socket: {
			path: '/tmp/ecat.sock',
		},
		slaves: `${__dirname}/config/slaves.json`,
		frequency: 1000,
		interval: 0,
		autostart: true,
	};

	fs.writeFileSync(__configPath, JSON.stringify(_tmp));

	return configFile();
}

const response = {
	_stringify(obj){
		return JSON.stringify(obj, (key, value) =>
            typeof value === 'bigint'
                ? value.toString()
                : value
		);
	},

	success(command, code, message){
		const obj = {
			status: true,
			command,
			code: code % 100,
			message
		};

		return this._stringify(obj);
	},

	error(command, code, message){
		const obj = {
			status: false,
			command,
			code: (code % 100) + 100,
			message
		};

		return this._stringify(obj);
	},
};

function _initSocket(socketPath){
	configs.socket = new __SOCKET(
			configs.path,
			configs.port,
			false,
			true,
		);

	configs.socket.create();

	configs.socket.on('raw', async received => {
		const cmds = received.toString('utf8').split(/[\x00]+/g);

		for(const cmd of cmds){
			if(cmd){
				processCommand(cmd);
			}
		}
	});
}

function _startEtherlab(){
	configs.etherlab = new __ETHERLAB(configs.slaves, configs.frequency);

	configs.etherlab.start();

	configs.etherlab.on('data', (data, period) => {
		configs.received.data = data;
		configs.received.period = period;

		if(!configs.onData){
			return;
		}

		configs.socket.send(
				response.success('data', 20, data)
			);
	});

	configs.etherlab.on('state', state => {
		configs.socket.send(
				response.success('state', 20, state)
			);
	});

	configs.etherlab.on('ready', async () => {
		configs.socket.send(
				response.success('ready', 20)
			);
	});

	configs.etherlab.on('error', async () => {
		configs.socket.send(
				response.error('error', 20)
			);
	});
}

function _setConfig(received){
	const {values} = received;

	try{
		for(const key in values){
			const val = values[key];

			switch(key){
				case 'path':
				case 'ip':
					configs.path = val;
					break;

				case 'port':
					configs.port = val;
					break;

				case 'slaves':
					configs.slaves = val;
					break;

				case 'frequency':
					configs.frequency = Math.round(val);
					break;

				case 'interval':
					configs.interval = Math.round(val);
					break;

				case 'autostart':
					configs.autostart = Boolean(val);
					break;

				default:
					break;
			}

			configFileWrite();

			return response.success(
					'config',
					20,
					'Restart application in order to apply new configuration'
				);
		}
	} catch (error) {
		return response.success(
				'config',
				20,
				error
			);
	}
}

async function processCommand(received){
	let _command;

	try{
		received = JSON.parse(received);

		_command = received.command.trim();
		const {index, value, unit} = received;

		switch(_command){
			case 'config':
				const proc = _setConfig(received);
				configs.socket.send(proc);
				break;

			case 'start':
				_startEtherlab();
				break;

			case 'frequency':
				const freq = configs.etherlab.setFrequency(value);
				configs.socket.send(response.success(_command, 20, freq));
				break;

			case 'domain':
				const domain = await configs.etherlab.getDomain();
				configs.socket.send(response.success(_command, 20, domain));
				break;

			case 'write':
				const write = configs.etherlab.writeIndex(index, value);
				configs.socket.send(response.success(_command, 20, write));
				break;

			case 'data':
				const data = await configs.etherlab.getValues();
				configs.socket.send(response.success(_command, 20, data));
				break;

			case 'state':
				const state = configs.etherlab.getMasterState();
				configs.socket.send(response.success(_command, 20, data));
				break;

			case 'period':
				const period = configs.etherlab.getLatencyAndJitter(unit);
				configs.socket.send(response.success(_command, 20, period));
				break;

			case 'toggleLiveData':
				configs.onData ^= 1;
				break;
		}
	} catch(error) {
		configs.socket.send(response.error(_command, 20, {received, error}));
	}
}

(async()=>{
	configs = configFileRead();
	_initSocket(configs.path);

	if(configs.autostart){
		_startEtherlab();
	}
})();

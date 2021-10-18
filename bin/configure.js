#!/usr/bin/env node

const fs = require('fs');
const path = require('path');

function _CONFIG_(configs = {}){
	const {
			socket,
			path,
			port,
			slaves,
			frequency,
			interval,
			autostart
		} = configs;

	this.socket = {
		type: socket && socket.type,
		path: socket && socket.path,
		port: socket && socket.port,
	};
	this.slaves = slaves;
	this.frequency = frequency;
	this.interval = interval;
	this.autostart = autostart;
}

function readConfigFile(filepath){
	try{
		if(fs.existsSync(filepath)){
			const contents = fs.readFileSync(filepath);
			const tmp = JSON.parse(contents);

			if(!tmp instanceof Object){
				throw `Invalid config file (${typeof(tmp)})`
			}

			return {
				_config: new _CONFIG_(tmp),
				_status: true,
				msg: 'Valid config file',
			};
		}

		throw `Config file doesn't exist (${filepath})`
	} catch(error) {
		return {
			_config: new _CONFIG_(),
			_status: false,
			msg: error,
		};
	}
}

function writeConfigFile(filepath, config){
	const status = (() => {
		try{
			fs.writeFileSync(filepath, JSON.stringify(config));
			return true;
		} catch (error) {
			console.error('Invalid config file', error);
			return false;
		}
	})();

	if(!status){
		console.error(`Error at writing config into file '${filepath}'`, status);
		return status;
	}

	console.log(config, '\n');
	console.log(`Succesfully write config into file '${filepath}'`, status);

	return status;
}

(() => {
	let {argv} = process;
	const _filepath = `${__dirname}/config/conf.json`;
	const {_config, _status, msg} = readConfigFile(_filepath);

	if(argv.includes('--read') || argv.includes('-r')){
		console.log(`----------- ${msg} ---------------\n\n`, _config, '\n');
		return;
	}

	let socketType = {
		tcp: true,
		domain: true,
	};

	while(argv.length > 0){
		const param = argv.shift();
		const value = argv.shift();

		switch(param){
			case '-p':
			case '--path': {
				if(socketType.domain){
					_config.socket.type = 'domain';
					_config.socket.path = value;
					_config.socket.port = undefined;

					socketType.tcp = false;
				}

				break;
			}

			case '-t':
			case '--tcp':
			case '--ip': {
				if(socketType.tcp){
					const ipPort = value.split(':');
					_config.socket.type = 'tcp';
					_config.socket.path = ipPort[0];
					_config.socket.port = ipPort[1];

					if(!_config.socket.port){
						_config.socket.port = 10000;
					}

					socketType.domain = false;
				}
				break;
			}

			case '-s':
			case '--slave': {
				const slavePath = path.resolve(value);
				if(fs.existsSync(slavePath)){
					_config.slaves = slavePath;
				}
				break;
			}

			case '-f':
			case '--frequency': {
				if(!isNaN(value)){
					_config.frequency = +value;
				}
				break;
			}

			case '-i':
			case '--interval': {
				if(!isNaN(value)){
					_config.interval = +value;
				}
				break;
			}

			case '--autostart': {
				if(+value === 0 || +value === 1){
					_config.autostart = +value;
				}
				break;
			}

			default:
				break;
		}
	}

	if(_config.autostart == undefined){
		_config.autostart = 1;
	}

	if(_config.interval == undefined){
		_config.interval = 0;
	}

	writeConfigFile(_filepath, _config);
})();

const fs = require('fs');

const EventEmitter = require('events');
const ecat = require('bindings')('ecat');
const {hrtime} = process;

const MovingAvg = require('./class/movingAverage.class.js');

const _config = {
	slaveJSON: undefined,
	data: undefined,
	state: undefined,
	interval: 0n,
	frequency: 1000,
	domain: undefined,
};

const _cycle = {
	frequency: 1000,
	period: 0,
	latency: {
		current: 0n,
		last: 0n,
		diff: 0n,
	},
	timer: 0n,
};

const _average = {
	lastN: 10,
	jitter: new MovingAvg(this.lastN),
	latency: new MovingAvg(this.lastN),
	get values(){
		return {
			latency: this.latency.val,
			jitter: this.jitter.val,
		}
	},
};

class ECAT extends EventEmitter{
	constructor(slaveJSON, freq){
		super();
		const self = this;

		self._timer = 0n;
		self._isReady = false;

		self.init(slaveJSON, freq);
	}

	/**
	 *	emit data if only there is at least 1 listener
	 *	@private
	 *	@param {string} eventName - event name to be emitted
	 *	@param {*} value - event data, any datatype can be emitted
	 * */
	_emit(eventName, ...values){
		const self = this;

		if(self.listenerCount(eventName) > 0){
			self.emit(eventName, ...values);
		}
	}

	setSlaveConfigPath(_path){
		if(typeof(_path) !== 'string'){
			throw `path must be in string! (${typeof(_path)})`;
			return;
		}

		if(!fs.existsSync(_path)){
			throw `File Not Found! '${typeof(_path)}'`;
			return;
		}

		_config.slaveJSON = _path;
	}

	setFrequency(freq){
		if(isNaN(freq) || !Number.isInteger(freq) || freq <= 0){
			throw `Frequency must be an integer and greater than 0`;
		}

		_cycle.frequency = freq;
		_cycle.period = ecat.setFrequency(_cycle.frequency);
		_config.frequency = _cycle.frequency;

		return _cycle;
	}

	init(...args){
		const self = this;

		const configPath = args[0];
		const freq = args[1];

		self.setSlaveConfigPath(configPath);
		self.setFrequency(freq);
	}

	_calcLatency(){
		const self = this;
		_cycle.latency.current = hrtime.bigint() - _cycle.timer;
		_cycle.timer = hrtime.bigint();

		if(_cycle.latency.last == 0n){
			_cycle.latency.last = _cycle.latency.current;
			return;
		}

		_cycle.latency.diff = Math.abs(
				Number(_cycle.latency.last - _cycle.latency.current)
			);
		_cycle.latency.last = _cycle.latency.current;

		_average.jitter.add(Number(_cycle.latency.diff));
		_average.latency.add(Number(_cycle.latency.current));
	}

	stop(){
		return ecat.stop();
	}

	start(){
		const self = this;

		if(!_config.slaveJSON){
			throw 'Slave filepath is undefined!';
		}

		try{
			ecat.init(_config.slaveJSON);

			_cycle.timer = hrtime.bigint();
			self._timer = _cycle.timer;

			ecat.start((...args) => {
				try{
					const data = args[0];
					const state = args[1];
					const isOperational = Boolean(ecat.isOperational());

					if(_config.state != state){
						self._emit('state', state);

						if(!self._isReady && isOperational){
							self._emit('ready', isOperational);
							self._isReady = isOperational;
						}
					}

					_config.state = state;

					// if slaves are not operational, skip emitting data
					if(!isOperational){
						// reset timer
						_cycle.timer = hrtime.bigint();
						self._timer = _cycle.timer;
						return;
					}

					const current = hrtime.bigint();
					self._calcLatency();

					if(!_config.interval || current - self._timer >= _config.interval){
						self._emit('data', data, _cycle.latency.current);
						self._timer = hrtime.bigint();
					}

					_config.data = data;
				} catch(error) {
					console.error('start Error', error);
				}
			});
		} catch(error) {
			self._emit('error', error);
		}
	}

	writeIndex(...args){
		if(args.length === 2){
			return ecat.writeIndex(args[0], args[1]);
		}

		if(typeof(args[0]) === 'object'){
			const {index, value} = args[0];
			if(index != undefined && value != undefined){
				return ecat.writeIndex(index, value);
			}
		}

		return -1;
	}

	writeIndexes(arr){
		const self = this;

		const statuses = [];
		for(const item of arr){
			statuses.push(self.writeIndex(item));
		}

		return statuses;
	}

	_toNanoseconds(val, unit){
		val = BigInt(val);

		switch(unit){
			case 'us':
				return val * BigInt(1e3);
				break;

			case 'ms':
				return val * BigInt(1e6);
				break;

			case 's':
				return val * BigInt(1e9);
				break;

			default:
				return val;
				break;

		}
	}

	fromNanoseconds(val, unit){
		val = Number(val);

		switch(unit){
			case 'us':
				return val / 1e3;
				break;

			case 'ms':
				return val / 1e6;
				break;

			case 's':
				return val / 1e9;
				break;

			default:
				return val;
				break;

		}
	}

	setInterval(val, unit = 'ms'){
		const self = this;
		_config.interval = self._toNanoseconds(val, unit);
	}

	getDomain(){
		return _config.domain || ecat.getAllocatedDomain();
	}

	getLatencyAndJitter(unit = 'us'){
		const self = this;
		const values = _average.values;

		for(const key in values){
			values[key] = self.fromNanoseconds(values[key], unit);
		}

		return {...values, unit};
	}

	getMasterState(){
		return ecat.getMasterState();
	}

	getValues(){
		return ecat.getDomainValues();
	}

	liveData(state){
		return ecat.setLiveData(+state & 0x01);
	}
}

module.exports = ECAT;

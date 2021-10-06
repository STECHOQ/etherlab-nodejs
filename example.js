const __etherlab = require('.');

const config = './slaves.json';
const frequency = 5000;

const etherlab = new __etherlab(config, frequency);

etherlab.on('data', (data, latency) => {
	console.log(data, latency);
	console.log(etherlab.getLatencyAndJitter());
});

etherlab.on('state', current => {
	console.log('master\'s current state ', current);
});

etherlab.on('ready', async () => {
	const domain = await etherlab.getDomain();
	console.log(domain);
});

etherlab.on('error', error => {
	console.error(error);
});

etherlab.start();

let val = 0;
setInterval(() => {
	val %= 0x1000;

	const x = [
		(((val & 0xf) >> 0 )& 1),
		(((val & 0xf) >> 1 )& 1),
		(((val & 0xf) >> 2 )& 1),
		(((val & 0xf) >> 3 )& 1)
	];

	etherlab.writeIndex(12, x[0]);
	etherlab.writeIndex(13, x[1]);
	etherlab.writeIndex(14, x[2]);

	etherlab.writeIndex({
			index:15,
			value: x[3]
		});

	etherlab.writeIndexes([
		{index: 0, value: val},
		{index: 1, value: val+1},
	]);

	val += 7;
}, 25);

setInterval(() => {
	etherlab.liveData(false);
}, 3000);

setTimeout(() => {
	setInterval(async () => {
		const state = etherlab.getMasterState();
		const data = await etherlab.getValues();
		const period = etherlab.getLatencyAndJitter();

		console.log(state, data, period);
	}, 1000);
}, 6000);

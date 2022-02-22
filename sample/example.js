const __etherlab = require('..');

//~ const config = __dirname + '/slaves.sample.json';
const config = [{"alias":0,"position":0,"vendor_id":"0x00000002","product_code":"0x044c2c52","revision":"0x00120000","serial":"0x00000000"},{"alias":0,"position":1,"vendor_id":"0x00000002","product_code":"0x0fa43052","revision":"0x00140000","serial":"0x00000000","syncs":[{"index":2,"watchdog_enabled":false,"direction":"output","pdos":[{"index":"0x1600","entries":[{"index":"0x7000","subindex":"0x01","size":16,"add_to_domain":true,"swap_endian":false,"signed":false}]},{"index":"0x1601","entries":[{"index":"0x7010","subindex":"0x01","size":16,"add_to_domain":true,"swap_endian":false,"signed":false}]},{"index":"0x1602","entries":[{"index":"0x7020","subindex":"0x01","size":16,"add_to_domain":true,"swap_endian":false,"signed":false}]},{"index":"0x1603","entries":[{"index":"0x7030","subindex":"0x01","size":16,"add_to_domain":true,"swap_endian":false,"signed":false}]}]}]},{"alias":0,"position":2,"vendor_id":"0x00000002","product_code":"0x03ec3052","revision":"0x00130000","serial":"0x00000000","syncs":[{"index":0,"watchdog_enabled":false,"direction":"input","pdos":[{"index":"0x1a00","entries":[{"index":"0x6000","subindex":"0x01","size":1,"add_to_domain":true,"swap_endian":false,"signed":false}]},{"index":"0x1a01","entries":[{"index":"0x6010","subindex":"0x01","size":1,"add_to_domain":true,"swap_endian":false,"signed":false}]},{"index":"0x1a02","entries":[{"index":"0x6020","subindex":"0x01","size":1,"add_to_domain":true,"swap_endian":false,"signed":false}]},{"index":"0x1a03","entries":[{"index":"0x6030","subindex":"0x01","size":1,"add_to_domain":true,"swap_endian":false,"signed":false}]}]}]},{"alias":0,"position":3,"vendor_id":"0x00000002","product_code":"0x18503052","revision":"0x00150000","serial":"0x00000000","syncs":[{"index":2,"watchdog_enabled":false,"direction":"output","pdos":[{"index":"0x1601","entries":[{"index":"0x7010","subindex":"0x01","size":8,"add_to_domain":true,"swap_endian":false,"signed":false}]}]},{"index":3,"watchdog_enabled":false,"direction":"input","pdos":[{"index":"0x1a00","entries":[{"index":"0x6000","subindex":"0x01","size":16,"add_to_domain":true,"swap_endian":true,"signed":false}]},{"index":"0x1a02","entries":[{"index":"0x6020","subindex":"0x01","size":8,"add_to_domain":true,"swap_endian":true,"signed":false}]}]}],"parameters":[{"index":"0x8000","subindex":"0x04","size":32,"value":"0x55"},{"index":"0x8000","subindex":"0x05","size":32,"value":"0x014d"},{"index":"0x8000","subindex":"0x20","size":8,"value":"0x11"},{"index":"0x8000","subindex":"0x21","size":8,"value":"0x2b"},{"index":"0x8000","subindex":"0x24","size":8,"value":"0xc2"},{"index":"0x8000","subindex":"0x28","size":16,"value":"0x23"},{"index":"0x8010","subindex":"0x28","size":16,"value":"0x02"},{"index":"0x8020","subindex":"0x28","size":16,"value":"0x01"}]}];

const frequency = 500;
const etherlab = new __etherlab(config, frequency, true);

const timezoneOffset = (new Date()).getTimezoneOffset() * 60000;
const values = [0x0000, 0x7fff];
let index = 0;

console.log('Started at ', new Date(Date.now() - timezoneOffset));

function sleep(ms){
	return new Promise(resolve => setTimeout(resolve, ms))
};

function write(){
	etherlab.write(1, 0x7000,0x01, values[index]);
	etherlab.write(1, 0x7010,0x01, values[index ^ 1]);
	etherlab.write(1, 0x7020,0x01, Date.now() % 0x8000);
	etherlab.write(1, 0x7030,0x01, 0x7ff0);

	etherlab.write(3, 0x7010,0x01, index^1);

	index ^= 1;
}

function read(period){
	const vals = `Pos 1 ${(0x7000).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(1, 0x7000, 0x01)).toString(16) + '\n'
		+ `Pos 1 ${(0x7010).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(1, 0x7010, 0x01)).toString(16) + '\n'
		+ `Pos 1 ${(0x7020).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(1, 0x7020, 0x01)).toString(16) + '\n'
		+ `Pos 1 ${(0x7030).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(1, 0x7030, 0x01)).toString(16) + '\n'
		+ `Pos 2 ${(0x6000).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(2, 0x6000, 0x01)).toString(16) + '\n'
		+ `Pos 2 ${(0x6010).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(2, 0x6010, 0x01)).toString(16) + '\n'
		+ `Pos 2 ${(0x6020).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(2, 0x6020, 0x01)).toString(16) + '\n'
		+ `Pos 2 ${(0x6030).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(2, 0x6030, 0x01)).toString(16) + '\n'
		+ `Pos 3 ${(0x7010).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(3, 0x7010, 0x01)).toString(16) + '\n'
		+ `Pos 3 ${(0x6020).toString(16)}:0x01 ` + '=> ' + Number(etherlab.read(3, 0x6020, 0x01)).toString(16) + '\n'
		+ `Cyclic Task ` + period +` us` + '\n';

	//~ console.log(vals);
}

etherlab.on('data', (data, period) => {
	read(period);
});

etherlab.on('state', current => {
	console.log(
			(new Date(Date.now() - timezoneOffset)).toISOString(),
			'\t|\tmaster\'s current state ',
			current,
			etherlab.getMasterStateDetails(),
		);
});

etherlab.on('ready', async state => {
	console.log('ready', state);
	const domain = await etherlab.getDomain();
	setInterval(() => {
		write();
	}, 25);
});

etherlab.on('error', error => {
	console.error(error);
});

etherlab.start();

/*** stop and restart ***/
let msStart = Date.now();
let stopCounter = 0;

function loop(){
	const msDiff = Date.now() - msStart;
	if(msDiff > 10000){
		msStart = Date.now();
		console.log('======================================== STOP', ++stopCounter,'=========================================================');
		console.log('Stopped at ', new Date(Date.now() - timezoneOffset));
		etherlab.stop();

		console.log('STOP STATUS', etherlab.getMasterStateDetails());

		setTimeout(() => {
			etherlab.start();
			console.log('Started at ', new Date(Date.now() - timezoneOffset));
			msStart = Date.now();
		}, 1000);
	}

	setTimeout(loop, 1);
}

loop();

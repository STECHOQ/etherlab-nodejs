const Ethercat = require('..');

const ecat = new Ethercat(`${__dirname}/slaves.json`, 5000);

let isReady = false;

const values = [
		3000,
		2000,
		1000,
		0,
	];

const sleep = ms => new Promise(r => setTimeout(r, ms));

test('Get Slaves name', async () => {
	const slaves = [
		ecat.sdoRead(1, 0x1008, 0x00, 6),
		ecat.sdoRead(2, 0x1008, 0x00, 6),
	];

	expect(slaves[0].toString()).toBe('EL3174');
	expect(slaves[1].toString()).toBe('EL4004');
});

test('Starting Ethercat Master', async () => {
	ecat.start();

	ecat.on('error', error => console.error(error));
	ecat.on('ready', () => { isReady = true });

	const timeStart = Date.now();
	while(!isReady) {
		await sleep(5);
		if(Date.now() - timeStart > 3000) break;
	}

	const { OP } = ecat.getMasterStateDetails();
	expect(OP).toEqual(1);
});

test('Match Attached Slaves\' domains', async () => {
	const domains = ecat.getMappedDomains();

	const expected = [
		{ pos: 1, index: 0x6000, subindex: 0x11 },
		{ pos: 1, index: 0x6010, subindex: 0x11 },
		{ pos: 1, index: 0x6030, subindex: 0x11 },
		{ pos: 1, index: 0x6020, subindex: 0x11 },
		{ pos: 2, index: 0x7000, subindex: 0x01 },
		{ pos: 2, index: 0x7010, subindex: 0x01 },
		{ pos: 2, index: 0x7020, subindex: 0x01 },
		{ pos: 2, index: 0x7030, subindex: 0x01 },
	];

	for(const item of expected){
		const id = `${item.pos}`
			+ `:${item.index.toString(16).padStart(4, '0')}`
			+ `:${item.subindex.toString(16).padStart(2, '0')}`;

		expect(domains[id]).toBeDefined();
	}
});

test('Write AnaOut Ch. 0', async () => {
	const value = values[0];

	ecat.write(2, 0x7000, 0x01, value);

	await sleep(50);

	const read = ecat.read(2, 0x7000, 0x01);

	expect(read).toEqual(value);
});

test('Read AnaIn Ch. 0', async () => {
	const value = values[0];

	await sleep(150);

	const read = ecat.read(1, 0x6000, 0x11);

	expect(Math.abs(read - value)).toBeLessThan(250);
});

test('Write AnaOut Ch. 1', async () => {
	const value = values[1];

	ecat.write(2, 0x7010, 0x01, value);

	await sleep(50);

	const read = ecat.read(2, 0x7010, 0x01);

	expect(read).toEqual(value);
});

test('Read AnaIn Ch. 1', async () => {
	const value = values[1];

	await sleep(150);

	const read = ecat.read(1, 0x6010, 0x11);

	expect(Math.abs(read - value)).toBeLessThan(200);
});

test('Restart Ethercat Master', async () => {
	let timeStart, state;

	ecat.stop();

	timeStart = Date.now();
	while(1) {
		await sleep(5);

		state = ecat.getMasterStateDetails();
		if(!state.OP) break;

		if(Date.now() - timeStart > 3000) break;
	}

	state = ecat.getMasterStateDetails();
	expect(state.OP).toEqual(0);
	expect(state.PREOP).toEqual(1);

	ecat.start();

	timeStart = Date.now();
	while(1) {
		await sleep(5);

		state = ecat.getMasterStateDetails();
		if(state.OP) break;

		if(Date.now() - timeStart > 3000) break;
	}

	state = ecat.getMasterStateDetails();
	expect(state.OP).toEqual(1);
	expect(state.PREOP).toEqual(0);
});

test('Write AnaOut Ch. 2', async () => {
	const value = values[2];

	ecat.write(2, 0x7020, 0x01, value);

	await sleep(50);

	const read = ecat.read(2, 0x7020, 0x01);

	expect(read).toEqual(value);
});

test('Read AnaIn Ch. 2', async () => {
	const value = values[2];

	await sleep(150);

	const read = ecat.read(1, 0x6020, 0x11);

	expect(Math.abs(read - value)).toBeLessThan(200);
});

test('Write AnaOut Ch. 3', async () => {
	const value = values[2];

	ecat.write(2, 0x7030, 0x01, value);

	await sleep(50);

	const read = ecat.read(2, 0x7030, 0x01);

	expect(read).toEqual(value);
});

test('Read AnaIn Ch. 3 (NC)', async () => {
	const read = ecat.read(1, 0x6030, 0x11);
	expect(Math.abs(read)).toBeLessThan(25);
});

test('Write 0 to All AnaOut', async () => {
	for(let index = 0x7000; index <= 0x7030; index += 0x10){
		ecat.write(2, index, 0x01, 0);
	}

	await sleep(150);

	for(let index = 0x7000; index <= 0x7000; index += 0x10){
		const read = ecat.read(2, index, 0x01);
		expect(read).toEqual(0);
	}
});

test('All AnaIn must be close to 0', async () => {
	await sleep(100);

	for(let index = 0x6000; index <= 0x6030; index += 0x10){
		const read = ecat.read(1, index, 0x11);
		expect(read).toBeLessThan(25);
	}
});

test('Stopping Ethercat Master', async () => {
	ecat.stop();

	const timeStart = Date.now();
	while(1) {
		await sleep(5);

		const state = ecat.getMasterStateDetails();
		if(!state.OP) break;

		if(Date.now() - timeStart > 3000) break;
	}

	const { OP, PREOP } = ecat.getMasterStateDetails();
	expect(OP).toEqual(0);
	expect(PREOP).toEqual(1);

	ecat.removeAllListeners('error');
	ecat.removeAllListeners('ready');
});

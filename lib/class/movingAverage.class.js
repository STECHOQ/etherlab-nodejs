// source http://www.zrzahid.com/moving-average-of-last-n-numbers-in-a-stream/

class MovingAvgLastN{
	constructor(N){
		this.maxTotal = N;
		this.lastN = [];
		this.avg = 0;
		this.head = 0;
		this.total = 0;
	}

	add(num){
		let prevSum = this.total * this.avg;

		if(this.total == this.maxTotal){
			prevSum -= this.lastN[this.head];
			this.total--;
		}

		this.head = (this.head + 1) % this.maxTotal;
		const emptyPos = (this.maxTotal + this.head-1) % this.maxTotal;
		this.lastN[emptyPos] = num;

		const newSum = prevSum + num;
		this.total++;
		this.avg = newSum / this.total;

		return this;
	}

	get val(){
		return this.avg;
	}
}

module.exports = MovingAvgLastN;

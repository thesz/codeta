#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <math.h>
#include <bit>
#include <filesystem>
#include <vector>


#define NYD do { fprintf(stderr, __FILE__ ":%d" ": not yet done\n", __LINE__); exit(1); } while(0)

#define A(e) do { if (!(e)) { fprintf(stderr, __FILE__ ":%d" ": %s failed\n", __LINE__, #e); exit(1); }} while(0)

const uint64_t req_size = 100000000ULL;
static_assert((req_size % 16) == 0);
const uint64_t context_size = 64;
uint8_t buffer_[context_size + req_size];
uint8_t* buffer = &buffer_[context_size];

#define	BITS	(8)

struct encoded { bool bits[BITS]; };
void encode(encoded* enc, int c) {
	int j=0;
	uint64_t code = c % (1 << BITS);
	for (int i=0;i<BITS;i++) {
		enc->bits[i] = (code >> i) & 1;
	}
} /* encode */

struct reqbits {
	uint8_t b[req_size/8];
	void set(void) {
		memset(b, ~0, sizeof(b));
	}
	void set(int i) {
		int by = i/8;
		int bi = i % 8;
		int m = 1 << bi;
		b[by] |= m;
	}
	void clear(void) {
		memset(b, 0, sizeof(b));
	}
	void clear(int i) {
		int by = i/8;
		int bi = i % 8;
		int m = 1 << bi;
		b[by] &= ~m;
	}
	void flip(int i) {
		int by = i/8;
		int bi = i % 8;
		int m = 1 << bi;
		b[by] ^= m;
	}
	bool bit(int i) const {
		int by = i/8;
		int bi = i % 8;
		int m = 1 << bi;
		return (b[by] & m) != 0;
		
	}
	int popcount(void) const {
		int s = 0;
		for(int i=0;i<req_size/8;i++) {
			s += std::popcount(b[i]);
		}
		return s;
	}
	int accurate(void) const {
		int pc = popcount();
		if (pc < req_size) {
			return req_size - pc;
		}
		return pc;
	}
};

static char table_name[500];
static char temp_table_name[510];
static char* get_table_name(int bit) {
	sprintf(table_name, "table-%d-bit-%d.inc", BITS, bit);
	sprintf(temp_table_name, "%s.temp", table_name);
	return table_name;
} /* get_table_name */
static bool table_written(int bit) {
	char* fn = get_table_name(bit);
	return std::filesystem::exists(fn);
} /* table_written */

struct context_bit {
	uint8_t byte, bit;
	context_bit() { }
	context_bit(int a, int b) : byte(a), bit(b) { }
	bool operator==(const context_bit& a) const {
		return byte == a.byte && bit == a.bit;
	}
	bool operator!=(const context_bit& a) const {
		return byte != a.byte || bit != a.bit;
	}
	int cbit(int i) {
		int b = buffer[i - 0 - byte] >> bit;
		return b & 1;
	}
};
static std::vector<context_bit> enabled_byte_0;
static reqbits target;
bool setup_target(int bit) {
	if (bit >= BITS) {
		return false;
	}
	int flags[256];
	int i;
	for(i=0;i<256;i++) {
		encoded e;
		encode(&e, i);
		flags[i] = e.bits[bit];
	}
	target.clear();
	int o = 0;
	for(i=0;i<req_size;i++) {
		if (flags[buffer[i]]) {
			o ++;
			target.set(i);
		}
	}
	enabled_byte_0.clear();
	for(i=bit+1;i<BITS;i++) {
		context_bit cb(0,i);
		enabled_byte_0.push_back(cb);
	}
	int a = o*2 >= req_size ? o : req_size - o;
	double p = a;
	p /= req_size;
	printf("bit %d (from %d): %d ones, probability %g\n", bit, BITS, o, p);
	return true;
} /* setup_target */

struct denode {
	int this_index, parent;
	context_bit split;
	int child0, child1;
	int head = -1;
	double prob;
	double f;
	double bitlen;
	double accurate;
	double count;
	double accuracy() const { return accurate/(count + 1e-30); }
};
struct detree {
	std::vector<denode> nodes;
};
struct table {
	std::vector<detree> detrees;
};
static std::vector<table> tables;
static int* dn_index = nullptr;
static int* next = nullptr;
static double clamp(double x) { return std::max(1e-10, std::min(1-1e-10,x)); }
static void score_denode(denode& dn) {
	printf("scoring %d\n", dn.this_index);
	int i, c = 0;
	double wpsum = 0, wsum = 0;
	for(i=dn.head;i>=0;i = next[i]) {
		c++;
		dn_index[i] = dn.this_index;
		double w = 1.0/req_size;
		wpsum += target.bit(i) * w;
		wsum += w;
	}
	A(c && "  empty node???");
	double p = clamp(wpsum / wsum);
	double f = 0.5 * log(p/(1-p));
	printf("  prob %g, f %g, count %d, wsum %g\n", p, f, c, wsum);
	dn.prob = p;
	dn.f = f;
	double wbitlen = -wsum * (p * log2(p) + (1-p)*log2(1-p));
	double accurate = 0;
	for(i=dn.head;i>=0;i=next[i]) {
		double x = f;
		int bit = target.bit(i);
		int acc = (x >= 0) == (bit > 0);
		accurate += acc;
	}
	double accuracy = accurate / c;
	printf("  weighted bitlen %g, accuracy %g\n", wbitlen, accuracy);
	dn.bitlen = wbitlen;
	dn.accurate = accurate;
	dn.count = c;
} /* score_denode */
static void grow_table(void) {
	double pred_acc = 0;
	int i, j, k;
	std::vector<detree> detrees;
	do {
		for(i=0;i<req_size-1;i++) {
			next[i] = i+1;
		}
		next[i] = -1;
		std::vector<denode> denodes;
		denode t;
		t.this_index = 0;
		t.parent = t.child0 = t.child1 = -1;
		t.head = 0;
		score_denode(t);
		int unsplit_count = 1;
		double tree_accurate = t.accurate;
		double tree_bitlen = t.bitlen;
		denodes.push_back(t);
		std::vector<int> front;
		front.push_back(0);
		while (unsplit_count < 65536 && front.size() > 0 && tree_bitlen > 0.01) {
			printf("front size %d, unsplit %d, tree bitlen %g, tree accuracy %g\n", int(front.size()), unsplit_count, tree_bitlen, tree_accurate/req_size);
			denode cdn;
			{
				double longest_bitlen;
				int front_longest;
				for(i=0;i<front.size();i++) {
					double bl = denodes[front[i]].bitlen;
					if (i==0 || longest_bitlen < bl) {
						longest_bitlen = bl;
						front_longest = i;
					}
				}
				cdn = denodes[front[front_longest]];
				front[front_longest] = front[front.size()-1];
				front.resize(front.size() - 1);
			}
			printf("node %d, count %g, bitlen %g, accuracy %g\n", cdn.this_index, cdn.count, cdn.bitlen, cdn.accuracy());
			if (cdn.accuracy() > 0.9995) {
				printf("  node is accurate enough, keep as-is\n");
				continue;
			}
			int ci = cdn.this_index;
			int pi = cdn.parent;
			int head = cdn.head;
			int max_byte = 0;
			std::vector<context_bit> parents_bits;
			while (pi >= 0) {
				auto b = denodes[pi].split;
				max_byte = std::max(max_byte, int(b.byte));
				//printf("add parent split %d/%d\n", b.byte, b.bit);
				parents_bits.push_back(b);
				pi = denodes[pi].parent;
			}
			max_byte += 40;
			max_byte = std::min(int(context_size), max_byte);
			std::vector<context_bit> cands;
			for(i=0;i<max_byte;i++) {
				for(j=0;j<8;j++) {
					context_bit cb(i, j);
					if (i==0) {
						if (std::find(enabled_byte_0.begin(), enabled_byte_0.end(), cb) == enabled_byte_0.end()) {
							continue;
						}
					}
					for(k=0;k<parents_bits.size() && parents_bits[k] != cb;k++) { }
					if (k>=parents_bits.size()) {
						//printf("add cand %d/%d/\n", cb.byte, cb.bit);
						cands.push_back(cb);
					}
				}
			}
			bool first = true;
			double best_wbl = 1e100;
			context_bit best_cb;
#pragma omp parallel for
			for(int candi=0;candi < cands.size(); candi++) {
//#pragma omp critical (print_cand)
//				printf("assessing %d/%d (%d)\n", cands[candi].byte, cands[candi].bit, candi);
				int counts[2] = {0, 0};
				double wpsum[2] = {0,0} , wsum[2] = {0, 0};
				for(int i=cdn.head;i>=0;i=next[i]) {
					int bit = cands[candi].cbit(i);
					counts[bit] ++;
					double w = 1.0/req_size;
					wpsum[bit] += w * target.bit(i);
					wsum[bit] += w;
				}
				if (counts[0] == 0 || counts[1] == 0) {
					continue;
				}
				double ps[2], fs[2];
				ps[0] = clamp(wpsum[0]/wsum[0]);
				ps[1] = clamp(wpsum[1]/wsum[1]);
				fs[0] = log(ps[0]/(1-ps[0]));
				fs[1] = log(ps[1]/(1-ps[1]));
				double wbl = -wsum[0]*(log2(ps[0])*ps[0] + log2(1-ps[0])*(1-ps[0]))
						-wsum[1]*(log2(ps[1])*ps[1] + log2(1-ps[1])*(1-ps[1]));
#pragma omp critical (check_set_best)
				{
					if (first || wbl < best_wbl) {
						best_wbl = wbl;
						best_cb.byte = cands[candi].byte;
						best_cb.bit = cands[candi].bit;

						printf("  recording %d/%d, weighted bit len %g\n", best_cb.byte, best_cb.bit, best_wbl);
						printf("    p0 %g, f0 %g, p1 %g, f1 %g\n", ps[0], fs[0], ps[1], fs[1]);
					}
					first = false;
				}
			}
			if (first) {
				printf("we were unable to find good split, max byte %d\n", max_byte);
				continue;
			}
			printf("best split %d/%d.\n", best_cb.byte, best_cb.bit);
			cdn.split = best_cb;
			denode child0, child1;
			child0.parent = child1.parent = cdn.this_index;
			child0.child0 = child0.child1 = child1.child0 = child1.child1 = -1;
			child0.this_index = denodes.size();
			child1.this_index = child0.this_index + 1;
			int* heads[2];
			heads[0] = &child0.head;
			heads[1] = &child1.head;
			for(int i = cdn.head;i>=0;i=next[i]) {
				int cbit = best_cb.cbit(i);
				*(heads[cbit]) = i;
				heads[cbit] = &next[i];
			}
			cdn.head = -1;
			*(heads[0]) = *(heads[1]) = -1;
			cdn.child0 = child0.this_index;
			cdn.child1 = child1.this_index;
			score_denode(child0);
			score_denode(child1);
			tree_bitlen -= cdn.bitlen;
			tree_bitlen += child0.bitlen + child1.bitlen;
			tree_accurate -= cdn.accurate;
			tree_accurate += child0.accurate + child1.accurate;
			denodes[cdn.this_index] = cdn;
			denodes.push_back(child0);
			denodes.push_back(child1);
			front.push_back(child0.this_index);
			front.push_back(child1.this_index);
			unsplit_count += 1;
		}
		double accurate = 0;
		double bl = 0;
#pragma omp parallel for reduction (+:accurate, bl)
		for(i=0;i<req_size;i++) {
			A(denodes[dn_index[i]].child0 < 0);
			double f = denodes[dn_index[i]].f;
			double z = 1/(1 + exp(-2*f));
			int bit = target.bit(i);
			int acc = (bit != 0) == (f >= 0);
			double lb = -(bit ? log2(z) : log2(1 - z));
			bl += lb;
			accurate += acc;
		}
		pred_acc = accurate / req_size;
		bl /= req_size;
		printf("adjusted accuracy %g, bitlen %g\n", pred_acc, bl);
		printf("weights rescaled\n");
		detree dt;
		dt.nodes = denodes;
		detrees.push_back(dt);
	} while(false);
} /* grow_table */

int main(void) {
	setvbuf( stdout, NULL, _IONBF, 0 );
	setvbuf( stderr, NULL, _IONBF, 0 );
	const char* fn = "enwik8";
	FILE* f = fopen(fn, "rb");
	A(f);
	memset(buffer_, 0x20, context_size);
	size_t rcnt = fread(buffer, sizeof(buffer[0]), req_size, f);
	A(rcnt == req_size);
	printf("fn %s, read %zu bytes (%zuM)\n", fn, rcnt, rcnt/1000000);
	next = (int*)calloc(req_size, sizeof(*next));
	A(next);
	dn_index = (int*)calloc(req_size, sizeof(*dn_index));
	A(dn_index);
	for(int i=BITS-1;i>=0;i--) {
		if (!setup_target(i)) {
			printf("stop.");
			break;
		}
		grow_table();
	}
	NYD;
	return 0;
} /* main */


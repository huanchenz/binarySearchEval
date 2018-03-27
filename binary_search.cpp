#include <sys/time.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include "stx-btree/include/stx/btree_map.h"
#include "stx-btree/include/stx/btree.h"
#include "ARTSynchronized/ART/Tree.h"

const uint64_t kRecordSize = 8; // unit: 64-bit
const uint64_t kNumRecords = 100000000;
const uint64_t kTestSize = 10000000;

typedef stx::btree_map<uint64_t, uint64_t> BTree;
typedef ART_unsynchronized::Tree ARTIndex;

double getNow() {
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

void print(const char* index_type, double time_diff, uint64_t sum) {
    std::cout << index_type << " time = " << time_diff << std::endl;
    std::cout << index_type << " tput = " << ((kTestSize + 0.0) / time_diff / 1000000) << " Mops/s" << std::endl;
    std::cout << index_type << " sum = " << sum << std::endl;
}

uint64_t primary_binary_search(uint64_t* table, uint64_t target) {
    uint64_t l = 0;
    uint64_t r = kNumRecords;
    while (l < r) {
	uint64_t m = (l + r) >> 1;
	if (target < table[m * kRecordSize]) {
	    r = m;
	} else if (target == table[m * kRecordSize]) {
	    return m;
	} else {
	    l = m + 1;
	}
    }
    return (kNumRecords + 1);
}

uint64_t secondary_binary_search(uint64_t* index, uint64_t* table, uint64_t target) {
    uint64_t l = 0;
    uint64_t r = kNumRecords;
    while (l < r) {
	uint64_t m = (l + r) >> 1;
	if (target < table[index[m] * kRecordSize + 1]) {
	    r = m;
	} else if (target == table[index[m] * kRecordSize + 1]) {
	    return m;
	} else {
	    l = m + 1;
	}
    }
    return (kNumRecords + 1);
}

// For ART
void loadKey(TID tid, Key &key) {
    key.setKeyLen(sizeof(uint64_t));
    reinterpret_cast<uint64_t *>(&key[0])[0] = __builtin_bswap64(tid);
}

void loadKeyPri(TID tid, Key &key) {
    key.setKeyLen(sizeof(uint64_t));
    reinterpret_cast<uint64_t *>(&key[0])[0] = __builtin_bswap64(*(uint64_t*)tid);
}

void loadKeySec(TID tid, Key &key) {
    key.setKeyLen(sizeof(uint64_t));
    reinterpret_cast<uint64_t *>(&key[0])[0] = __builtin_bswap64(*((uint64_t*)tid + 1));
}

int main() {
    // reserve space for table
    uint64_t* table = new uint64_t[kRecordSize * kNumRecords];

    // generate secondary index keys
    std::vector<uint64_t> secondary_keys;
    for (uint64_t i = 0; i < kNumRecords; i++) {
	secondary_keys.push_back(i);
    }
    std::random_shuffle(secondary_keys.begin(), secondary_keys.end());

    // populate table
    for (uint64_t i = 0; i < kNumRecords; i++) {
	table[i * kRecordSize] = i; // primary key is record ID
	table[i * kRecordSize + 1] = secondary_keys[i];
    }

    // build indexes -------------------------------------------------------------
    // primary index is implicit
    // build secondary index permutation
    uint64_t* secondary_index = new uint64_t[kNumRecords];
    for (uint64_t i = 0; i < kNumRecords; i++) {
	secondary_index[secondary_keys[i]] = i;
    }

    // build B+tree indexes
    // primary
    BTree* btree_pri = new BTree();
    for (uint64_t i = 0; i < kNumRecords; i++) {
	btree_pri->insert(i, (uint64_t)(table + i * kRecordSize));
    }
    // secondary
    BTree* btree_sec = new BTree();
    for (uint64_t i = 0; i < kNumRecords; i++) {
	btree_sec->insert(secondary_keys[i], (uint64_t)(table + i * kRecordSize));
    }

    // build ART indexes
    // primary
    ARTIndex* art_pri = new ARTIndex(loadKeyPri);
    for (uint64_t i = 0; i < kNumRecords; i++) {
	Key key;
	loadKey(i, key);
	art_pri->insert(key, (TID)(table + i * kRecordSize));
    }

    // secondary
    ARTIndex* art_sec = new ARTIndex(loadKeySec);
    for (uint64_t i = 0; i < kNumRecords; i++) {
	Key key;
	loadKey(secondary_keys[i], key);
	art_sec->insert(key, (TID)(table + i * kRecordSize));
    }

    // generate queries ----------------------------------------------------------
    uint64_t* targets = new uint64_t[kTestSize];
    //std::random_device rd;
    //std::mt19937_64 e(rd());
    std::mt19937_64 e(2018);
    std::uniform_int_distribution<uint64_t> dist(0, kNumRecords - 1);
    for (uint64_t i = 0; i < kTestSize; i++) {
	targets[i] = dist(e);
    }

    // measurement ---------------------------------------------------------------
    // binary search
    // primary
    double start_time = getNow();
    uint64_t sum = 0;
    for (uint64_t i = 0; i < kTestSize; i++) {
	uint64_t search = primary_binary_search(table, targets[i]);
	if (targets[i] == search)
	    sum += search;
    }
    double end_time = getNow();
    double time_diff = end_time - start_time;
    print("binary search primary", time_diff, sum);

    // secondary
    start_time = getNow();
    sum = 0;
    for (uint64_t i = 0; i < kTestSize; i++) {
	uint64_t search = secondary_binary_search(secondary_index, table, targets[i]);
	if (targets[i] == search)
	    sum += search;
    }
    end_time = getNow();
    time_diff = end_time - start_time;
    print("binary search secondary", time_diff, sum);

    // B+tree
    // primary
    BTree::const_iterator iter;
    start_time = getNow();
    sum = 0;
    for (uint64_t i = 0; i < kTestSize; i++) {
	iter = btree_pri->find(targets[i]);
	if (iter != btree_pri->end())
	    sum += (*(uint64_t*)(iter->second));
    }
    end_time = getNow();
    time_diff = end_time - start_time;
    print("B+tree primary", time_diff, sum);

    // secondary
    start_time = getNow();
    sum = 0;
    for (uint64_t i = 0; i < kTestSize; i++) {
	iter = btree_sec->find(targets[i]);
	if (iter != btree_sec->end())
	    sum += (*(uint64_t*)(iter->second + 8));
    }
    end_time = getNow();
    time_diff = end_time - start_time;
    print("B+tree secondary", time_diff, sum);

    // ART
    // primary
    start_time = getNow();
    sum = 0;
    for (uint64_t i = 0; i < kTestSize; i++) {
	Key key;
	loadKey(targets[i], key);
	uint64_t* val_ptr = (uint64_t*)art_pri->lookup(key);
	if (val_ptr)
	    sum += (*val_ptr);
    }
    end_time = getNow();
    time_diff = end_time - start_time;
    print("ART primary", time_diff, sum);

    // secondary
    start_time = getNow();
    sum = 0;
    for (uint64_t i = 0; i < kTestSize; i++) {
	Key key;
	loadKey(targets[i], key);
	uint64_t* val_ptr = (uint64_t*)art_sec->lookup(key);
	if (val_ptr)
	    sum += (*(val_ptr + 1));
    }
    end_time = getNow();
    time_diff = end_time - start_time;
    print("ART secondary", time_diff, sum);

    delete[] table;
    delete[] secondary_index;
    delete[] targets;

    return 0;
}

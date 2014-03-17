#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cwchar>
#include <cassert>
#include <cstdio>
#include <stdarg.h>

#define UNUSED __attribute__ ((unused))

#define BPLUSTREE_VERSION 0.1

static void print_license();
static void DBUG(const char *fmt,...);

// May compile & run on BSD, MacOSX, Solaris & AIX.
// But restricting implementation to linux as
// it's the only os platform that this code has
// been developed & thoroughly tested. Any one
// that is interested in running this code in OS
// other than linux should comment below macros.
// At your own risk beware!

#if !defined(__linux) || !defined(__linux__)
#error "Operating system not supported currently"
#endif

#include <unistd.h>
#include <getopt.h> // parsing command line arguments

using std::cout;
using std::cin;
using std::endl;

const int DEFAULT_ORDER = 4;


// A KEY Comparator abstraction. This is really very
// useful when there are keys of type char */wchar_t *
// that uses strncmp/wcsncmp respectively as comparision
// functions instead of native operators. It also helps
// to define a custom comparator API for completely different
// types of keys. This abstraction avoids to provide complete
// rewrite of Bplustree class specializations for special
// types of keys such as char */wchar_t* or any other user
// defined type that compares the keys in ways not using the
// native comparision operators!
template <typename KEY> 
class Comparator {
public:
	Comparator() {
	}

	virtual int compare(const KEY &_k1,const KEY &_k2) const {
		if (_k1 > _k2) return 1;
		else if (_k1 < _k2) return -1;
		return 0;
	}

	virtual ~Comparator() {
	}
};

// char* Comparator specialization 
template <> class Comparator<char *> {
public:
	Comparator() {
	}

	virtual int compare(const char *const &_k1,const char *const &_k2) const {
		size_t k1_len = strlen(_k1);
		int cmp = strncmp(_k1,_k2,k1_len);
		if (cmp > 0) return 1;
		else if (cmp < 0) return -1;
		return 0;
	}

	virtual ~Comparator() {
	}
};

// wchar_t* Comparator specialization
template <> class Comparator<wchar_t *> {
public:
	Comparator() {
	}

	virtual int compare(const wchar_t *const &_k1,const wchar_t *const &_k2) const {
		size_t k1_len = wcslen(_k1);
		int cmp = wcsncmp(_k1,_k2,k1_len);
		if (cmp > 0) return 1;
		else if (cmp < 0) return -1;
		return 0;
	}

	virtual ~Comparator() {
	}
};

// following template specializations will result in 
// compilation errors!
template <> class Comparator<void>;
template <> class Comparator<void*>;

// A simple in-memory B+Tree data structure.
// TODO: 
//	[1] mmap() + file (FD) based implementation for persistence
//	[2] concurrent & thread-safe index operations
template <typename KEY,typename VALUE>
class Bplustree {
class BplustreeNode;
class Stack;
public:
	explicit Bplustree(int _order = DEFAULT_ORDER) 
		: order(_order),root(0),key_compare() {
	}

	inline void insert(const KEY &_key,const VALUE &_value) {
		insert(root,_key,_value);
	}

	inline bool find(const KEY &_key,VALUE &_value) {
		return find(root,_key,_value);
	}

	inline bool is_empty() const {
		return root == 0;
	}
	
	inline void destroy() {
		destroy(root);
	}

	~Bplustree() {
		destroy();
	}
private:
// destroys the tree in top-down fashion recursively
	inline void destroy(BplustreeNode *_node) {
		if (_node == 0) return;
		if (!_node->is_leaf()) {
			for (int i = 0; i <= _node->num_keys; i++) {
				destroy(_node->ptrs[i]);
			}
		}
		delete _node;
		_node = 0;
	}
// insert helper function
	void insert(BplustreeNode *_node,const KEY &_key,const VALUE &_value) {
		if (_node == 0 && is_empty()) {
			root = new BplustreeNode(order,true);
			root->insert(_key,_value);
		} else {
			Stack parent_nodes;
			KEY median;
			BplustreeNode *leaf_node = find_leaf(root,_key,parent_nodes);
			assert(leaf_node != 0); // fail if leaf node can't be located
			assert(leaf_node->is_leaf()); // fail if it's not a leaf node
			if (!leaf_node->insert(_key,_value)) {
				// split leaf_node
				BplustreeNode *new_leaf = leaf_node->split(_key,_value,median);
				// move up the median key
				insert_to_parent(parent_nodes.pop(),median,leaf_node,new_leaf);
			}
		}
	}

	/*
		recursively pushes up the _key to the parent node.
		it can move all the way up to the root node.
		if root node is too full it creates a new root.
		if a new root is created the tree's height increases by 1
		from current height.
		The recursion stops when one of the following happens:
			[1] A new root is created and the median keys is pushed there
			[2] If the _parent is not full & the _key & ptrs are inserted there
    */
	void insert_to_parent(BplustreeNode *_parent,const KEY &_key,BplustreeNode *_left,
		BplustreeNode *_right,Stack &_parents) {
		if (_parent == 0) {
			/* A new root node is created */
			BplustreeNode *new_root = new BplustreeNode(order,false);
			new_root->keys[0] = _key;
			new_root->ptrs[0] = _left;
			new_root->ptrs[1] = _right;
			++new_root->num_keys;
			root = new_root;
			/* recursion stops here */
		} else {
			if (!_parent->is_full()) {
				_parent->insert(_key,_right);
				/* recursion stops here */
			} else {
				KEY median;
				BplustreeNode *new_parent_level_node = _parent->split(_key,_left,_right,median);
				/* recursively push the median key up to the parent */
				insert_to_parent(_parents.pop(),median,_parent,new_parent_level_node,_parents);
			}
		}
	}

// find helper function
	bool find(BplustreeNode *_node,const KEY &_key,VALUE &_value);
// find leaf function - used for insert/delete key-value functions
	BplustreeNode *find_leaf(BplustreeNode *_node,const KEY &_key,Stack &parent_nodes) {
		BplustreeNode *node = _node;
		int i = 0;
		if (node == 0) return 0;
		if (!node->is_leaf()) {
			parent_nodes.push(node);
		}
		while (node != 0 && !node->is_leaf()) {
			i = 0;
			while (i < node->num_keys) {
				if (_key >= node->keys[i]) i++;
				else break;
			}
			node = node->ptrs[i];
			if (node != 0 && !node->is_leaf()) {
				parent_nodes.push(node);
			}
		}
		return node;
	}
class BplustreeNode {
friend class Stack;
friend class Bplustree<KEY,VALUE>;
private:
	const int max_children;
	bool leaf;
	int num_keys;
	KEY **keys;
	VALUE **values;
	BplustreeNode **ptrs; /*>! ptrs- stores max_children nodes if index 
						   *>! ptrs- stores 2-nodes(prev & next) if leaf */
	Comparator<KEY> key_compare;
public:
	explicit BplustreeNode(
		int _max_children,bool _leaf = false) 
			: max_children(_max_children),leaf(_leaf),key_compare() {
		init();
	}

	inline bool is_leaf() const {
		return leaf;
	}

	inline BplustreeNode *prev() {
		if (!leaf) return 0;
		return ptrs[0];
	}

	inline BplustreeNode *next() {
		if (!leaf) return 0;
		return ptrs[1];
	}

	inline void set_next(BplustreeNode *_node) {
		if (leaf) {
			ptrs[1] = _node;
		}
	}

	inline void set_prev(BplustreeNode *_node) {
		if (leaf) {
			ptrs[0] = _node;
		}
	}

	/*
		inserts _key & _value to the leaf node.
		insertion will be based on _key's ascending order.
		Aborts if the node is not leaf.
		returns true if insertion is successful.
		returns false if the node is full.
     */
	bool insert(const KEY &_key,const VALUE &_value) {
		assert(leaf); // fail if it's index
		if (is_full()) {
			return false; // no space left in node
		}
		int slot = find_slot(_key);
		for (int i = num_keys; i > slot; i--) {
			keys[i] = keys[i - 1];
			values[i] = values[i - 1];
		}
		keys[slot] = _key;
		values[slot] = _value;
		incr();
		return true;
	}

	/*
		inserts _key & _node to the index node.
		insertion will be based on _key's ascending order.
		_node being the right child of the _key.
		Aborts if the node is not index.
		returns true if insertion is successful.
		returns false if the node is full;
	 */
	bool insert(const KEY &_key,BplustreeNode *_node) {
		assert(!leaf); // fail if it's leaf
		if (is_full()) {
			return false; // no space left in node
		}
		int slot = find_slot(_key);
		for (int i = num_keys; i > slot; i--) {
			keys[i] = keys[i - 1];
			ptrs[i + 1] = ptrs[i];
		}
		ptrs[slot + 1] = _node;
		keys[slot] = _key;
		incr();
		return true;
	}

	inline int find_slot(const KEY &_key) {
		//TODO: check for num_keys/max_children 
		// & do a binary search in case of 50(?) 
		// or more keys.
		return sequential_search(_key);
	}

	/* 
		split a full leaf node to two leaf nodes
		keeps first half of max_children/2 in the current (this) node
		moves second half of max_children/2 to the new leaf node
		copies the median key that needs to be pushed up to the parent node
		parent node is essentially an index node
		returns the new leaf node created during the split operation
	 */
	BplustreeNode *split(const KEY &_key,const VALUE &_value,KEY &_median) {	
		assert(is_leaf()); // fail if it's not a leaf
		BplustreeNode *new_leaf = 0;
		KEY *l_keys = new KEY[max_children];
		VALUE *l_values = new VALUE[max_children];
	
		int slot = find_slot(_key);
		int i = 0,j = 0;

		for (i = 0,j = 0; i < num_keys; i++,j++) {
			if (j == slot) j++;
			l_keys[j] = keys[i];
			l_values[j] = values[i];
		}
		l_keys[slot] = _key;
		l_values[slot] = _value;		
		new_leaf = new BplustreeNode(max_children,true);

		int split_pos = 0;
		int cut_order = max_children - 1;
		if (cut_order % 2 == 0) split_pos = cut_order/2;
		else split_pos = (cut_order/2) + 1;

	/* fix [0,max_children/2) entries to the current full node */
		num_keys = 0;
		delete [] keys;
		delete [] values;
		keys = new KEY[max_children - 1];
		values = new VALUE[max_children - 1];
		for (i = 0; i < split_pos; i++) {
			keys[i] = l_keys[i];
			values[i] = l_values[i];
			++num_keys;
		}
	/* move [max_children/2,max_children) entries to new leaf node */
		for (i = split_pos,j = 0; i < max_children; i++,j++) {
			new_leaf->keys[j] = l_keys[i];
			new_leaf->values[j] = l_values[i];
			++new_leaf->num_keys;
		}
	/* fix prev, next pointers */
		new_leaf->set_next(next());
		set_next(new_leaf);
		new_leaf->set_prev(this);
	/* copy the median key that needs to be inserted upto the parent */
		_median = new_leaf->keys[0];

		delete [] l_keys;
		delete [] l_values;
		return new_leaf;
	}

	/*
		splits a full index node to two index nodes
		keeps first half of max_children/2 keys & ptrs in current (this) index node
		moves second half of max_children/2 keys & ptrs to new_index node
		copies median key that needs to be pushed up to the parent node
		parent node is essentially an index node
		returns the new index node created by the split operation
	 */
	BplustreeNode *split(const KEY &_key,BplustreeNode *_left,
		BplustreeNode *_right,KEY &_median) {
		assert(!is_leaf()); // fail if it's not an index
		BplustreeNode *new_index = 0;
		KEY *l_keys = new KEY[max_children];
		BplustreeNode **l_ptrs = new BplustreeNode*[max_children + 1];
		int i =0, j = 0,split_pos = 0;

		if (max_children % 2 == 0) split_pos = max_children/2;
		else split_pos = (max_children/2) + 1;
		int slot = find_slot(_key);
		for (i = 0,j = 0; i < max_children; i++,j++) {
			if (j == slot) j++;
			l_keys[j] = keys[i];
		}

		for (i = 0,j = 0; i < max_children + 1; i++,j++) {
			if (j == slot) j++;
			l_ptrs[j] = ptrs[i];
		}

		l_ptrs[slot + 1] = _right;
		l_keys[slot] = _key;
		delete [] keys;
		delete [] ptrs;
		num_keys = 0;

		keys = new KEY[max_children - 1];
		ptrs = new BplustreeNode*[max_children];

		for (i = 0; i < split_pos; i++) {
			keys[i] = l_keys[i];
			ptrs[i] = l_ptrs[i];
			++num_keys;
		}		
		ptrs[i] = l_ptrs[i];
		_median = l_keys[split_pos - 1];
		for (++i,j = 0; i < max_children; i++,j++) {
			new_index->ptrs[j] = l_ptrs[i];
			new_index->keys[j] = l_keys[i];
			++new_index->num_keys;
		}
		new_index->ptrs[j] = l_ptrs[i];

		delete [] l_keys;
		delete [] l_ptrs;
		return new_index;
	}

	inline bool is_full() const {
		return num_keys == max_children - 1;
	}

	/* 
		deallocates all the memory.
		called by destructor.
		can be called explicitly as well.
     */
	void destroy() {
		if (keys != 0) delete [] keys;
		if (leaf && values != 0) delete [] values;
		if (ptrs != 0) delete [] ptrs;
		num_keys = -1;
		leaf = false;
	}

	~BplustreeNode() {
		destroy();
	}
private:
	int sequential_search(const KEY &_key,bool match_exact = false) {
		int slot = 0;
		while (slot < num_keys 
			&& key_compare.compare(_key,keys[slot]) == 1) slot++;
		return slot;
	}

	int binary_search(const KEY &_key,bool match_exact = false) {
		//TODO: Implement binary search
		return -1;
	}

// constructor helper function
	void init() {
		num_keys = 0;
		keys = new KEY[max_children - 1];
		if (leaf) {
			values = new VALUE[max_children - 1];
			ptrs = new BplustreeNode*[2];
		} else {
			values = 0;
			ptrs = new BplustreeNode*[max_children];
		}
	}
	inline void incr() { ++num_keys; }
	inline void decr() { --num_keys; }
// copy/assign disallowed
	BplustreeNode(const BplustreeNode &);
	const BplustreeNode &operator = (const BplustreeNode &);
};

const int order;
BplustreeNode *root;
Comparator<KEY> key_compare;

/*
 A Stack (LIFO Data Structure) is used to store parent nodes when 
 searching for a leaf node to insert a key in the find_leaf() 
 method of Bplustree.This is important when a leaf node is split 
 and the median key needs to be pushed to parent & in case the 
 parent is too full & needs split etc., which goes up till the root 
 node which happens to be full a new root node will be created to 
 accomodate the split triggered from the leaf node. Pop-ing from 
 the stack returns the immediate parent of the node in question & the
 pop operation bottoms up to root whose parent is null. Another way to 
 do these things is to keep a parent pointer in the BplustreeNode & 
 keep the parent pointer reference updated when ever the node 
 split happens. I find using stack is easy & quiet straight forward 
 process to implement the insert/split operation easily.
*/
class Stack {
private:
	struct Node {
		BplustreeNode *data;
		Node *next;
	};

	Node *head;
public:
	Stack() : head(0) {
	}

	inline void push(BplustreeNode *data) {
		Node *node = new Node();
		node->data = data;
		if (head == 0) {
			head = node;
		} else {
			node->next = head;
			head = node;
		}
	}

	inline BplustreeNode *pop() {
		if (head == 0) return 0;
		Node *tmp_node = head->next;
		BplustreeNode *data = head->data;
		delete head;
		head = tmp_node;
		return data;
	}

	~Stack() {
		while (head != 0) pop();
	}
private:
// copy/assign disallowed
	Stack(const Stack &);
	const Stack &operator = (const Stack &);
};
// copy/assign disallowed
Bplustree(const Bplustree<KEY,VALUE> &);
const Bplustree<KEY,VALUE> &operator = (const Bplustree<KEY,VALUE> &);
};

// following template instantiations would result in compilation error!
template<> class Bplustree<void ,void >;
template<typename KEY> class Bplustree<KEY,void >;
template<typename VALUE> class Bplustree<void ,VALUE>;
template<> class Bplustree<void *,void *>;
template<typename KEY> class Bplustree<KEY,void *>;
template<typename VALUE> class Bplustree<void *,VALUE>;

bool dbug = false;
bool quiet = false;
int order = DEFAULT_ORDER;

static void handle_options(int argc,char **argv);

// handles the command line arguments passed
static void handle_options(int argc,char **argv) {
	int oc = -1,d = -1;
	while ((oc = getopt(argc,argv,":qo:d:")) != -1) {
		switch (oc) {
			case 'q': 
				quiet = true;
				break;
			case 'o' :
				order = atoi(optarg);
				if (order < DEFAULT_ORDER) order = DEFAULT_ORDER;
				break;
			case 'd' :
				d = atoi(optarg);
				if (d == 1)
					dbug = true;
				break;
			case ':' : 
				std::cerr << argv[0] << " option -'" << char(optopt) << "' requires an argument" << endl;
				break;
			case '?':
			default:
				break;
		};
	}
}

static void print_license() {
	if (!quiet) {
		cout << "BPLUSTREE " << BPLUSTREE_VERSION 
			 << endl << "Unless & otherwise stated all"
			 << " this code is licensed under Apache2.0"
			 << " license."<< endl << "Copyright (c) 2014 - 15." 
			 << endl << "Author: Kalyankumar Ramaseshan"
			 << endl << "email: rkalyankumar@gmail.com"
			 << endl << endl;
	}
}

static void DBUG(const char *fmt,...) {
	if (dbug) {
		va_list args;
		va_start(args,fmt);
		vfprintf(stdout,fmt,args);
		va_end(args);
		fprintf(stdout,"\n");
	}
}

int main(int argc,char **argv)
{
	std::ios::sync_with_stdio(false);
	handle_options(argc,argv);
	print_license();
	DBUG("%s","Test");
	Bplustree<int,int> tree;
	return 0;
}


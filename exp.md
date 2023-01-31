# lab 2 B+ Tree Index
## knowledge
* a clustering index is an index whose search key also defines the sequential order of the file
* indices whose search key specifies an order different from the sequential order of the file are called nonclustering indices
* all files are ordered sequentially on some search key. Such files, with a clustering index on the search key, are called index-sequential files
### dense and sparse indices
* Dense index: 
1. one index entry map to one search-key
2. index entry contains search-key value and a pointer to the first record with that search-key, records with the same search-key value are stored sequentially after the first record (for clustering index)
3. index must store a list or pointers to all records with the same search-key value. (for nonclustering index)
* Sparse index(only for clustering index): 
1. index entry contains a search-key value and a pointer to the first data record with that search-key value.
2. look up should find the largest index less or equal than the index, and look up sequentiallly. 


**Dense index is faster to locate a record, but sparse index require less space and less maintenance overhead for insertion and deletion**

### multi-level indices
* 1000000 tuples, 100 indexs fits in a 4-kilobyte block, need 10000 block, use binary search, require $ceil(log_210000)$ times rand io.
* use outer index on origin index, 10000 block need 100 block outer index, small enough to fit in main memory, just need 1 rand io, much fater.
* for very large database, use multi-level indices

### second indice
Secondary indices must be dense, with an index entry for every search-key value, and a pointer to every record in the file

### B+-Tree index file
* root [2, n] children
* non-leaf [ceil(n / 2), n] children 
* leaf [ceil((n - 1) / 2), n - 1] values

* a leaf-node split may require tens or even hundreds of I/O operations to update all affected secondary indices, making it a very expensive operation. solution: store the values of the primary-index search-key attributes.This approach thus greatly reduces the cost of index update due to file reorganization, although it increases the cost of accessing data using a secondary index.
* bottom-up b+ tree construction: after sorting the entries as we just described, we break up the sorted entries into blocks, keeping as many entries in a block as can fit in the block; the resulting blocks form the leaf level of the B+-tree. The minimum value in each block, along with the pointer to the block, is used to create entries in the next level of the B+- tree, pointing to the leaf blocks. Each further level of the tree is similarly constructed using the minimum values associated with each node one level below, until the root is created.
* B-tree, 
advantage: no duplicated key in internal node and leaf node
disadvantage: inorder range query, hard to travel total relation, less fanout(because  it could store record in internal node), more deeper, when update,complilcated

### hash indices
* widely used technique for building indices in main memory
* may be transiently created to process a join operation, or may be a permanent structure in a main memory database
* hash file organizations are not very widely used

### covering indices
* Covering indices are indices that store the values of some attributes (other than the search-key attributes) along with the pointers to the record

### Write-optimized Index Structure
#### LSM

#### Buffer Tree

### Bitmap Indices

### Indexing of Spatial and Temporal Data
## bugs 

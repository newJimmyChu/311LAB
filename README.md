# 311LAB
CS311 大作业：实现云存储中间层
导师为Patrick McDaniel    

主要代码为  
lcloud_filesys.c： 实现了lcopen, lclose, lcseek, lcread, 和lcwrite    
lcloud_client.c：客户端向服务器请求数据的socket实现  
lcloud_cache.c：用Doubly-Linked-List结构实现的LRU缓存  
所有代码均为原创  

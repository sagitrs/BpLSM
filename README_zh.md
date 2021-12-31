# Segment Buffer Skiplist

#### 介绍
SB-Skiplist is used to maintain a collection of intervals, unlike interval trees, which are designed to maintain intervals that do not overlap each other.

#### 软件架构
软件架构说明


#### 安装教程

1.  xxxx
2.  xxxx
3.  xxxx

#### 使用说明

1.  xxxx
2.  xxxx
3.  xxxx

#### 参与贡献

1.  Fork 本仓库
2.  新建 Feat_xxx 分支
3.  提交代码
4.  新建 Pull Request


#### 详细介绍

SB-Skiplist被用于维护一个区间集合，与区间树不同的是，它旨在维护那些不相互交叠的区间。
对于那些容易与其他区间发生交叠的区间，我们将其滞留在SB-Skiplist的中间节点上，因此不同于其他的树，SB-Skiplist的中间节点对用户是透明的，任何中间节点都会一个缓冲池。
这个数据结构到此为止并不完整，因为缺少一个方案使文件从缓冲区中离开，否则缓冲区将越积越多最终沦为一个普通的buffer。一个健康的SB-Skiplist应当控制中间节点的缓冲区内的元素不超过总元素一定比例，例如10%。
用户必须手动将缓冲区内的数据移出，这就意味着用户可能需要一些建议，例如“现在哪个节点缓冲文件最多”，“哪个节点内的缓冲文件被访问最多”等等。显然SB-Skiplist需要一些辅助以满足和用户的交互需求。
为此我们提供了两种插件：
1. 统计器：统计器可以统计每个节点——包括中间节点——的读写情况，以及如果用户需要的话，可以通过迭代器为更多统计器内的标签赋值。用户可以通过迭代器获得统计信息。
2. 评分器：评分器可以为每棵子树给出一个评分。这个评分函数需要由用户手动填写，它拥有访问SB-Skiplist的所有权限。简单的评分器可以只是返回当前节点buffer内的区间数量，复杂的评分器则需要用户对树的结构有更清晰的了解。
借助以上两种工具，用户将可以在O(N)的时间内找到树内评分最高的节点。我们计划尽快优化这一功能到O(LogN)的时间。

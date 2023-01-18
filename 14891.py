from collections import deque

toothA = deque(list(input()))
toothB = deque(list(input()))
toothC = deque(list(input()))
toothD = deque(list(input()))
teeth = [toothA, toothB, toothC, toothD]
K = int(input())
# print(toothA)
# print(toothB)
# print(toothC)
# print(toothD)

def rotate_left(deq_list):
    first = deq_list.popleft()
    deq_list.append(first)

def rotate_right(deq_list):
    last = deq_list.pop()
    deq_list.appendleft(last)

def leftcheck(left, right, direction):
    if left == 0:
        return
    
    if teeth[left-1][2] != teeth[right-1][6]:
        leftcheck(left-1, right-1, direction*(-1))
        if direction == -1: rotate_right(teeth[left-1])
        elif direction == 1: rotate_left(teeth[left-1])
        return
    elif teeth[left-1][2] == teeth[right-1][6]:
        return

def rightcheck(left, right, direction):
    if right == 5:
        return

    if teeth[left-1][2] != teeth[right-1][6]:
        rightcheck(left+1, right+1, direction*(-1))
        if direction == -1: rotate_right(teeth[right-1])
        elif direction == 1: rotate_left(teeth[right-1])
        return
    elif teeth[left-1][2] == teeth[right-1][6]:
        return

def targetRotate(target, direction):
    if direction == -1: rotate_left(teeth[target-1])
    elif direction == 1: rotate_right(teeth[target-1])
    return

for i in range(K):
    x = list(map(int, input().split(' ')))
    target = x[0]
    direction = x[1]
    leftcheck(target-1, target, direction)
    rightcheck(target, target+1, direction)
    targetRotate(target, direction)

# print(teeth)
score = (int)(teeth[0][0])*1 + (int)(teeth[1][0])*2 + (int)(teeth[2][0])*4 + (int)(teeth[3][0])*8
print(score)

# Welcome to Cat-Lang language

```
________________________________________
|               |\__/,|   (`\          |
|             _.|o o  |_   ) )         |
|-------------(((---(((----------------|
|      ___          ___                |
|     /  /\        /  /\        ___    |
|    /  /:/       /  /::\      /  /\   |
|   /  /:/       /  /:/\:\    /  /:/   |
|  /  /:/  ___  /  /:/~/::\  /  /:/    |
| /__/:/  /  /\/__/:/ /:/\:\/  /::\    |
| \  \:\ /  /:/\  \:\/:/__\/__/:/\:\   |
|  \  \:\  /:/  \  \::/    \__\/  \:\  |
|   \  \:\/:/    \  \:\         \  \:\ |
|    \  \::/      \  \:\         \__\/ |
|     \__\/        \__\/               |
|______________________________________|
```

## Info

This project does a  compiler using LLVM in CPP 20.

...

## syntax

### variable

```python
let a:int = 1;
let a:double = 1.1;
let:str = "hello, catlang!"
let p:ptr<int> = &a;
// local letiable
{
    let a:int=2;
    ...
}
```
### type inference

```python
let a = 1;
let b = "aaa";

```


### control flow

```python
if (true)
{
    print("True");
}
else
{
    print("false");
}
```

### loop

only support while

developing for loop in python style...

```python
while(cond){
    statement...;
}
```

### function

closure? overload?

```python
def add(x:int, y:int)->int
{
    return x+y;
}
```

### list


```python
let l:list<int> = [1, 2, 3];
let s:int = l[0];

let l2:list<list<int>> = [[1, 2, 3], [4, 5, 6]];
let s2:list<int> = l[0];
let t2:int = s[0];

```

### class


```python
class Point {
    let x:int;
    let y:int;
}

impl Point {
    def get_x(self:Point)->int {
        return self.x;
    }
    def get_y(self:Point)->int {
        return self.y;
    }
    def add(self:Point, other:Point)->Point {
        return Point(self.x + other.x, self.y + other.y);
    }
    def scale(self:Point, factor:int)->Point {
        return Point(self.x * factor, self.y * factor);
    }
    def distance_sq(self:Point, other:Point)->int {
        let dx:int = self.x - other.x;
        let dy:int = self.y - other.y;
        return dx * dx + dy * dy;
    }
}

class Circle {
    let center:Point;
    let radius:int;
}

impl Circle {
    def area(self:Circle)->int {
        return self.radius * self.radius * 3;
    }
    def contains(self:Circle, p:Point)->int {
        let dist_sq:int = self.center.distance_sq(p);
        let r_sq:int = self.radius * self.radius;
        if dist_sq < r_sq {
            return 1;
        } else {
            return 0;
        }
    }
}

def main()->int {
    let p1: Point = Point(1, 2);
    let p2: Point = Point(4, 6);

    let p3: Point = p1.add(p2);               // (5, 8)
    let p4: Point = p3.scale(2);              // (10, 16)

    let c: Circle = Circle(p4, 5);
    let area: int = c.area();                 // 5*5*3 = 75

    let inside: int = c.contains(Point(10, 10));

    let sum: int = 0;
    let i: int = 0;
    while i < 10 {
        sum = sum + i;
        i = i + 1;
    }   // sum = 45

    let p5: Point = p1.add(p2).scale(3);      // (15, 24)

    return area + inside + sum + p5.get_x() + p5.get_y();
}
`

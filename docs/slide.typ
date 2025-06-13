#import "@preview/touying:0.6.1": *
#import "@preview/touying-buaa:0.2.0": *
#import "@preview/pinit:0.2.2": *
#import "@preview/cetz:0.3.4"
#import "@preview/lilaq:0.2.0" as lq
#import "bilingual-bibliography.typ": *
#import "@preview/showybox:2.0.4": *
#import "authors.typ": *
#import "@preview/codly:1.3.0": *
#import "@preview/codly-languages:0.1.8": *
#import "@preview/fletcher:0.5.8" as fletcher: diagram, node, edge, shapes


#show: codly-init.with()
#codly(languages: codly-languages)
#codly(zebra-fill: none, stroke: 2pt+black, inset: 0.15em)
#let todo(what) = [
  #text(fill:color.red)[TODO: #what]
]
// Specify `lang` and `font` for the theme if needed.
#show: buaa-theme.with(
  lang: "zh",
  font: ("Times New Roman", "SimSun"),
  config-info(
    title: [基于SMT求解和变量级变异的约束用例生成器],
    subtitle: [软件分析与测试实践项目-工程题目],
    author: [
      #for author in authors [
        #author.name_cn
        #author.student_id
      ]
    ],
    date: datetime.today(),
    institution: [],
  ),
)
#show math.equation.where(block: true): set text(size: 20pt)
#show raw: set text(font: ("Cascadia Code", "SimHei"), size: 14pt)
#title-slide()

// === 需求分析 ===
= 需求分析

== 问题描述

  - 在软件测试中，手动构造高质量的测试用例是一项耗时且复杂的任务。特别是对于具有复杂数据结构和约束条件的系统。
  
  *实现目标：* 针对以C语言语法定义的约束条件，*快速*、*自动* 地生成满足（或不满足）这些约束的测试用例。

  ```c
  int a,b,c,d;

void _CONSTRAINT()
{
    a > 5 && a < 10;
    b > a || b < c;
    a + b > 30;
    d > 30 || d < 10 || b < 6;
}
  ```

  #pagebreak()
  *输入：* 一个 `.c` 格式的类C11约束文件，包含：
  - *变量声明:* `int a;`, `struct S1 s[2];`
  - *类型定义:* `typedef struct { ... } S1;`
  - *约束函数:* `_CONSTRAINT() { a > 5; ... }`
  - *约束原语:* `_LENGTH(...)`, `GAUSSIAN(...)`

  *输出：*
  - *正例：* 完全满足所有约束的用例。
  - *负例：* 精确违反某个特定约束的用例。
  - *并行化：* 支持多机并行生成，且用例不重复。

// === 核心挑战 ===
== 核心挑战
  #grid(
    columns: (1fr, 1fr),
    rows: (1fr),
    gutter: 2em,
    [
      #showybox(
        title: [1. 语言解析与语义理解],
        "- 需要解析类C11约束中的变量、类型、数组、结构体以及表达式。",
      )
      #v(0.2em)
      #showybox(
        title: [2. 混合约束建模],
        "- 如何统一处理算术、逻辑、数组约束？",
        "- `_LENGTH` 或 `GAUSSIAN`等约束原语如何建模？",
      )
    ],
    [
      #showybox(
        title: [3. 定向负例生成],
        "- 如何生成“有用”的负例（只违反一个约束），而不是随机的无效数据？",
      )
      #v(1.2em)
      #showybox(
        title: [4. 并行化与唯一性],
        "- 如何高效并行生成样例？（而不是并行却实际降低性能）",
        "- 如何避免输出相同的样例？",
      )
    ]
  )



// === 整体架构 ===
= 架构设计
== 整体设计
- 我们设计的测试用例生成工具可以被分为多个阶段。
- 工具中每个用例生成流水线可以无锁并行。
#[

#v(2em)
#set align(center)
#set text(1.2em)

#box(fill: aqua.lighten(80%), inset: 10pt, radius: 4pt)[*输入* 约束文件.c]
#text(2em, math.arrow.r)
#box(fill: blue.lighten(80%), inset: 10pt, radius: 4pt)[*阶段一* 约束文本解析与AST构建]
#text(2em, math.arrow.r)
#box(fill: green.lighten(80%), inset: 10pt, radius: 4pt)[*阶段二* 语义分析和约束识别/建模]
#text(2em, math.arrow.r)
#box(fill: orange.lighten(80%), inset: 10pt, radius: 4pt)[*阶段三* 约束变异/生成用例]
#text(2em, math.arrow.r)
#box(fill: lime.lighten(80%), inset: 10pt, radius: 4pt)[*阶段四* 用例验证/去重]
#text(2em, math.arrow.r)
#box(fill: red.lighten(80%), inset: 10pt, radius: 4pt)[*输出* 测试用例]
]



// === 技术方案详解 ===
= 实现技术

== 阶段一：前端解析 (ANTLRv4)
- *目标：* 将类C11形式的约束代码，转换为易于处理的*抽象语法树 (AST)*。（实际上ANTLRv4 保存了parse过程中产生的所有内容，即生成CST，Concrete Syntax Tree）
- *工具：* *ANTLRv4*
- *流程：*
  1. 基于 `C11.g4` 文法文件（在ANTLR仓库中提供），生成parser C++源代码，编译生成代码得到可执行的parser。
  2. 解析器读取约束文件，输出AST。



== 阶段二：语义分析和约束识别/建模
*目标：* 分析AST，构建*符号表*和其它辅助数据结构，得到约束变量数据结构（包括*结构体*的定义，*变量类型*等）并调用Z3 API构建Z3支持的可求解的约束项，建立原始约束条件和z3约束之间的映射关系。

*流程：*
1. *变量建模：* 建模结构体和数组，结构体使用Tuple理论，静态长度数组使用Array理论，动态长度数组（指针）使用Seq理论。
    例如`s[0].a`  #text(1.5em, math.arrow.r)  `(a (select s 0))`，其中a是投影函数，将tuple投影为字段。
2. *约束翻译：* 将C表达式转换为Z3的逻辑表达式。例如
    `a > 5 && a < 10` #text(1.5em, math.arrow.r) `And(a > 5, a < 10)`
3. *约束原语分离：* 识别 `_LENGTH` 和 `GAUSSIAN`，将其存入后处理指令列表。并记录所需的相关信息。

== 阶段三：约束变异求解与生成

#slide(repeat: 3)[
  *目标：* 求解约束并生成最终用例。

  *流程：* 原创的变量级随机赋值策略（Variable-level Random
 Assignment Strategy）
][
 #alternatives[
  #image("image-1.png")
 ][
  #image("image-2.png")
 ][
  #image("image-3.png")
 ]
]

== 阶段四： 用例验证/去重

- 为保证最终交付用例的100%正确性，项目引入了基于QuickJS的独立验证机制。本项目生成的样例JSON形式和对应的约束声明如下所示。

#grid(
  columns: (1fr,1fr),
  gutter: 1em,
  [
    ```json
{
    "a": 0,
    "b": [0, 0, 0],
    "s": [{
            "a": 0,
            "b": -1834331339,
            "c": 0.9329867027,
            "d": [] 
          },{
            "a": 8,
            "b": -783029840,
            "c": 2.0,
            "d": [] 
            } ]
}
```],
[
    ```c
typedef struct {
    int a;
    int b;
    double c;
    int* d;
} S1;

int a;
int b[3];
struct S1 s[2];
```
  ]
)

#pagebreak()

#v(2em)
- 因为JSON是JavaScript的子集，本项目可以方便地使用JavaScript解释器QuickJS独立验证生成样例地正确性。

```js
var f = () => {
    var _LENGTH = (e) => {
        return e.length;
    };
    var GAUSSIAN = (v, miu, va) => {
        return true;
    };
    Object.assign(this,
        {
            "a": 6, "b": [0, 0, -1],
            "s": [
              {"a": 7, "b": 0, "c": 2.0, "d": [14,15,16,17,18,19,0]}, 
              {"a": 11, "b": 0, "c": 3.0, "d": [14,15,16,17,18,19, 0]}
            ]
        }
    );
    return (a > 5 && a < 10) &&
           (b[1] > b[2] || b[0] < b[1]) && 
           (a + b[0] != s[0].a) && 
           (_LENGTH(s[0].d) > 6 && _LENGTH(s[0].d) < 10) &&
           (_LENGTH(s[1].d) > 6 && _LENGTH(s[1].d) < 10) && 
           (s[0].d[6] + s[1].d[6] == s[0].b * s[1].b);
};
f();

```
- 这种验证方法的优势在于相对于重新实现样例验证逻辑，QuickJS引擎本身的正确性是可以保证的（它通过Test262: ECMAScript Test Suite 和模糊测试进行了较为完整的测试），所以本项目使用QuickJS可以有效验证生成样例的正确性。

- 本项目直接使用hash表对样例去重，JSON的Hash值和“相等”在其树结构上递归定义。

== 并行化用例生成
#v(0.5em)
- 本项目实现的并行样例生成架构中，各线程不会访问某个共享内存位置，也就不会因为互斥锁导致性能损失。log打印到各线程的缓冲区，各线程生成的样例互不重叠。使用`std::thread`标准库API兼容各平台。
```cpp
template<typename... T>
void println_local(fmt::format_string<T...> fmt, T &&...args) {
    auto iter = fmt::format_to(std::back_inserter(local_log), fmt, args...);
    fmt::format_to(iter, "\n");
}
```
```cpp
for (auto i = 1; i <= thread_num; i++) {
  if (i == thread_num) {
      case_per_thread = remained_cases;
  }
  threads.emplace_back(core_runner, cons_src, output, case_per_thread, start_case_num, i);
  start_case_num.first += case_per_thread.first;
  start_case_num.second += case_per_thread.second;
}
```


= 工具测试
 
== 功能测试
#v(2em)
#table(
    columns: (1fr, 3fr),
    stroke: 1.5pt+black,
    [*测试用例文件*], [*测试目的*],
    [simple_val.c],[基础功能验证：测试最简单的整型变量和基本关系运算（`>`、`<`、`==`）。],
    [free_array.c],[一维数组测试：验证对一维数组的声明、索引访问和约束处理],
    [ndim_array.c],[多维数组测试：验证对二维及更高维度数组的正确解析和符号化。],
    [cons3.c],[算术运算测试：集中测试加（`+`）、减（`-`）、乘（`*`）、除（`/`）、模（`%`）等算术表达式的转换与求解。],
    [cons4.c],[逻辑运算测试：测试逻辑与（`&&`）、逻辑或（`||`）、逻辑非（`!`）以及它们组合形成的复杂逻辑链。],
    [cons2.c],[结构体与指针约束：重点测试对`struct`的定义、`struct`数组成员的访问以及对指针的 `_LENGTH` 特殊原语约束。],
    [cons5.c],[混合约束测试：结合算术运算、结构体成员访问和数组索引，测试系统在复杂混合场景下的处理能力。],
    [cons1.c],[综合样例：作为提供给用户的标准样例，覆盖了大部分核心功能，包括基本类型、数组、结构体、`_LENGTH`和`GAUSSIAN`原语。],

  )
*测试结果：*
所有功能测试用例均成功执行。测试结果表明，本工具能够正确解析所有测试场景中的变量声明和约束逻辑，并生成符合预期的正例和负例。所有生成的用例均100%通过了独立的QuickJS验证流程，证明了工具在功能层面的正确性和健壮性。

== 性能测试
- 测试任务：生成10,000个正例和10,000个负例，共计20,000个测试用例。

- 约束文件 (`cons_complex.c`)：为充分评估工具在复杂场景下的性能，我们设计了一个包含多种数据结构和大量约束的 `cons_complex.c` 文件。该文件定义了多个结构体，声明了包含基本类型、数组、结构体数组在内的多种变量，并在 `_CONSTRAINT` 函数中设置了超过20条相互关联的约束。


*运行命令*：

- 单线程：`./main -n 20000 -p 0.5 -c ./constraint-examples/cons_complex.c`
- 多线程：`./main -n 20000 -p 0.5 -c ./constraint-examples/cons_complex.c -j14`

#pagebreak()
#v(1em)
#table(
    columns: (1fr, 1fr),
    stroke: 1.5pt+black,
    [*测试模式*], [*完成20,000个用例耗时(正负例各1/2)*],
    [单线程],[> 20分钟],
    [多线程（14线程）],[约 3分钟],

)

1.  单线程性能：在单线程模式下，面对`cons_complex.c`这样复杂的约束集，*生成20,000个用例的耗时超过了20分钟*。这反映了在复杂场景下，串行化地进行“范围推导 -> 随机赋值 -> 约束求解 -> 验证输出”这一系列操作，其计算成本是相当高的。

2.  多线程性能与加速比：切换到14线程并行模式后，完成相同任务的总时间减少至约3分钟，*性能提升了近7倍*。这一显著提升得益于我们精心设计的并行架构：每个线程都在独立的求解环境和随机种子的引导下工作，任务被有效分摊到多个CPU核心上，且线程间无需通信和同步，有效避免了锁竞争和资源冲突。这使得计算能力能够随着核心数的增加而得到有效扩展。

// === 总结与Q&A ===
= 总结

- 本项目实现的工具*功能完备*，完整覆盖了设计需求，能够正确处理包括基本类型、多维数组、结构体、指针以及特殊原语在内的多种C语言特性和约束。

- 本项目实现的工具具有较为优秀的性能。工具在处理复杂约束和大规模用例生成任务时表现出色，其*多线程架构带来了显著的性能提升*。


#focus-slide[
  #set align(center)
  #v(3fr)
  #text(2.5em)[Q&A]
  #v(3fr)
] 
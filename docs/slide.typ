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
    subtitle: [],
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
    gutter: 2em,
    [
      #showybox(
        title: [1. 语言解析与语义理解],
        "- 需要解析类C11约束中的变量、类型、数组、结构体以及表达式。",
      )
      #v(0.2em)
      #showybox(
        title: [2. 混合约束建模],
        "- 如何统一处理算术、逻辑、数组和元约束？",
        "- Z3等标准求解器无法直接理解 `_LENGTH` 或 `GAUSSIAN`。",
      )
    ],
    [
      #showybox(
        title: [3. 定向负例生成],
        "- 如何生成“有用”的负例（只违反一个约束），而不是随机的无效数据？",
      )
      #v(0.2em)
      #showybox(
        title: [4. 并行化与唯一性],
        "- 如何在分布式环境下高效生成用例？",
        "- 如何从根本上避免多个节点生成重复的用例？",
      )
    ]
  )



// === 整体架构 ===
== 整体设计
  - 我们设计的测试用例生成工具可以被分为多个阶段。
  - 工具中每个用例生成流水线可以无锁并行。

  #v(2em)
  #set align(center)
  #set text(1.2em)

  #box(fill: aqua.lighten(80%), inset: 10pt, radius: 4pt)[约束文件.c]
  #text(2em, math.arrow.r)
  #box(fill: blue.lighten(80%), inset: 10pt, radius: 4pt)[*阶段一* 解析与AST构建]
  #text(2em, math.arrow.r)
  #box(fill: green.lighten(80%), inset: 10pt, radius: 4pt)[*阶段二* 建模与转换]
  #text(2em, math.arrow.r)
  #box(fill: orange.lighten(80%), inset: 10pt, radius: 4pt)[*阶段三* 求解与生成]
  #text(2em, math.arrow.r)
  #box(fill: red.lighten(80%), inset: 10pt, radius: 4pt)[测试用例]


// === 技术方案详解 ===
= 技术方案详解

== 阶段一：前端解析 (ANTLR4)
*目标：* 将人类可读的约束代码，转换为机器可读的*抽象语法树 (AST)*。
*工具：* #text(blue)[*ANTLR4*]
*流程：*
1. 定义 `.g4` 文法文件，描述我们的C-like语言。
2. ANTLR4 根据文法生成解析器。
3. 解析器读取约束文件，输出AST和符号表（记录所有变量信息）。



== 阶段二：语义分析与模型转换
*目标：* 将AST翻译成Z3求解器可以理解的数学公式。
*核心任务：*
1. *变量扁平化：* 将结构体和数组访问转换为独立变量。
    `s[0].a`  #text(1.5em, math.arrow.r)  `s_0_a`
2. *约束翻译：* 将C表达式转换为Z3的逻辑表达式。
    `a > 5 && a < 10` #text(1.5em, math.arrow.r) `And(a > 5, a < 10)`
3. *元约束分离：* 识别 `_LENGTH` 和 `GAUSSIAN`，将其存入“后处理指令列表”。

== 阶段三：约束求解与用例生成
*目标：* 求解约束并生成最终用例。
*工具：* #text(green)[*Z3 Solver*] + #text(orange)[*QuickJS*]
*流程：*
1. *求解：* Z3 求解器处理所有算术和逻辑约束，找到一个可行的解（模型）。
2. *后处理：*
    - 根据Z3解出的长度，为 `_LENGTH` 相关指针分配内存并填充。
    - 调用 #text(orange)[*QuickJS*] 引擎执行JS脚本，为 `GAUSSIAN` 等变量生成符合特定分布的值。
3. *格式化输出：* 组合所有值，生成最终用例文件。


// === 关键特性实现 ===
= 关键特性实现 

== 如何处理元约束？
我们采用“分离处理”的策略，让专业工具做专业的事。

*1. `_LENGTH(s[0].d) > 6`*
- 在建模阶段，引入一个全新的Z3整数变量 `s_0_d_length`。
- 将原始约束转换为对新变量的约束：`s_0_d_length > 6`。
- Z3解出 `s_0_d_length` 的值后，在生成阶段按此长度创建数组。

*2. `GAUSSIAN(s[0].c, 1.0, 1.0)`*
- 此约束*不*交给Z3。Z3只关心“真/假”，不关心概率分布。
- 建模阶段将其识别并记录为后处理指令：`{target: "s_0_c", gen: "gaussian", ...}`。
- 在生成阶段，调用 #text(orange)[*QuickJS*] 脚本引擎来执行生成逻辑，为 `s[0].c` 赋值。
#pause
*优势：* 极高的*扩展性*。未来增加 `UNIFORM` 或 `POISSON` 分布，只需添加新的JS脚本即可，无需改动核心求解器。

== 如何生成定向负例？
精准打击，而非盲目否定。

- *常规方法（效果差）：* `Not(C1 and C2 and C3)`。这会生成一个随机的、难以解释的无效用例。

- *我们的方法（精准）：* 要生成违反约束 `C3` 的负例，我们向求解器断言：
  ```typst
  #text(green)[C1] and #text(green)[C2] and #text(red)[Not(C3)]
  ```
- *结果：* 得到一个仅违反目标约束 `C3`，同时尽可能满足其他约束的、高质量的负例。


= 系统架构与并行化 

为了实现高效、不重复的并行生成，我们设计了如下分布式架构：

#figure(
  grid(
    columns: (1fr, 1fr, 1fr),
    align: center,
    [#showybox(title: "任务队列", "RabbitMQ / Redis")[
      - 存储生成任务
      - "生成100个正例"
      - "生成5个违反C3的负例"
    ]],
    [#showybox(title: "工作节点 (Workers)", "多台机器/多进程")[
      - 执行完整的生成流水线
      - 从队列获取任务
      - 生成用例
    ]],
    [#showybox(title: "结果与去重服务", "Redis Set")[
      - 存储已生成用例的哈希值
      - 利用 `SADD` 原子操作去重
    ]]
  ),
  caption: "分布式生成架构"
)

*唯一性保证流程：*
1. Worker生成一个用例后，计算其内容的哈希值。
2. 尝试将哈希写入Redis Set (`SADD`命令)。
3. 若写入成功，用例是新的，输出。
4. 若写入失败（哈希已存在），则向Z3添加排斥约束 `Not(当前解)`，重新求解以获得*下一个不同*的解。


// === 技术栈总结 ===
= 技术栈总结
  #table(
    columns: (1.5fr, 2fr, 3fr),
    stroke: .5pt,
    align: (center, left, left),
    [*工具*], [*角色定位*], [*核心价值*],
    
    [#text(blue)[*ANTLR4*]],
    [前端/解析器 (The Parser)],
    [连接用户意图和后端逻辑的桥梁，提供了强大的语言自定义能力。],

    [#text(green)[*Z3 Solver*]],
    [核心逻辑引擎 (The Brain)],
    [解决复杂的逻辑与算术约束，是保证用例正确性的基础。],

    [#text(orange)[*QuickJS*]],
    [可扩展生成引擎 (The Extensible Generator)],
    [处理程序化、随机化的生成任务，弥补了Z3的不足，为系统带来极佳的灵活性和扩展性。],
  )


// === 总结与Q&A ===
#slide(title: "总结与展望")[
  通过整合 ANTLR4、Z3 和 QuickJS，我们能够构建一个：
  - *自动化* 的测试用例生成系统，极大提升效率。
  - 能够理解复杂约束并生成*高质量*、*高覆盖率*用例的智能工具。
  - 具备*良好扩展性*，可轻松支持新的约束类型和值分布。
  - 支持*并行化*，能够快速应对大规模的生成任务。
]

#slide[
  #set align(center)
  #v(3fr)
  #text(3em)[Q & A]
  #v(3fr)
] 
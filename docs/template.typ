
#import "@preview/pointless-size:0.1.0": zh
#import "@preview/cuti:0.3.0": show-cn-fakebold
#import "bilingual-bibliography.typ": bilingual-bibliography
// 基于计算机学报
#let cactus-report(
  // The paper's title.
  title_cn: [论文标题],
  title_en: [Paper Title],
  course_name: [],
  

  // An array of authors. For each author you can specify a name,
  // department, organization, location, and email. Everything but
  // but the name is optional.
  authors: (),

  // 中英文摘要
  abstract_cn: none,
  abstract_en: none,

  // 关键词
  key_words_cn: (),
  key_words_en: (),

  // 纸张
  paper-size: "a4",

  // The result of a call to the `bibliography` function or `none`.
  bibliography: (),

  // 图标题
  image-supplement: [图],

  // 表标题
  table-supplement: [表],

  // 报告内容
  body
) = {
  // Set document metadata.
  set document(title: title_cn, author: authors.map(author => author.name_cn))

  // 模拟中文粗体
  show: show-cn-fakebold


  // 正文：小四号宋体、TNR
  set text(size: zh(-4), lang: "zh", font: ("Times New Roman", "SimSun"))

  // Enums numbering
  set enum(numbering: "1.")

  // 图表

  show math.equation.where(block: true): set text(top-edge: "baseline", bottom-edge: "baseline")
  show figure.where(kind: table): set figure(supplement: table-supplement, numbering: "1")
  show figure.where(kind: image): set figure(supplement: image-supplement, numbering: "1")
  // 表标题在上，图标题在下
  show figure.where(kind: table): set figure.caption(position: top)

  show figure.caption: it => {
    if it.kind == table {
      strong(it)
    } else {
      it
    }
  }
  show figure.where(kind: image): set figure.caption(position: bottom)
  // 三线表
  show table.cell.where(y: 0): strong

  set table(stroke: (x,y) => {
    if y == 0 {
      (top: 2pt + black, bottom: 1pt + black)
    }
  })
  // 最后一条线只能手动加了。。



  // Code blocks
  show raw: set text(font: "Times New Roman", ligatures: false, size: 1em / 0.8)

  // 页面、页眉、页脚
  let header(numbering: "1") = context [
    #set text(size: zh(5), font: ("Times New Roman", "KaiTi"))
    #h(1fr) #course_name 课程大作业：#title_cn #h(1fr) #counter(page).display(numbering)
    #line(length: 100%)
    // #context counter(footnote).update(0)
  ]
  set columns(gutter: 12pt)
  set page(
    // 双栏布局
    // columns: 2,
    paper: paper-size,
    // 页边距
    margin: (x: 41.5pt, top: 80.51pt, bottom: 89.51pt),
    header: header()
  )

  // Configure equation numbering and spacing.
  set math.equation(numbering: "(1)")
  show math.equation: set block(spacing: 0.65em)

  // Configure appearance of equation references
  show ref: it => {
    if it.element != none and it.element.func() == math.equation {
      // Override equation references.
      link(it.element.location(), numbering(
        it.element.numbering,
        ..counter(math.equation).at(it.element.location())
      ))
    } else {
      // Other references as usual.
      it
    }
  }

  // Configure lists.
  set enum(indent: 10pt, body-indent: 9pt)
  set list(indent: 10pt, body-indent: 9pt)

  // 标题编号模式，一级标题小三号、二级4好、三级小四
  show heading: set block(above: 1.4em, below: 1em)
  show heading: it => [
    #it
    #par(box())
  ]
  set heading(numbering: "1.1.1")
  show heading.where(level: 1): set text(size: zh(-3))
  show heading.where(level: 2): set text(size: zh(4))
  show heading.where(level: 3): set text(size: zh(-4))

  
  // 段落格式设置
  set text(top-edge: 0.75em, bottom-edge: -0.25em)
  
  // 中英文摘要
  place(
    top,
    float: true,
    scope: "parent",
    clearance: 10pt,
    {
      v(3pt, weak: true)
      align(center, par(leading: 0.5em, text(size: 24pt, title_cn)))
      v(8.35mm, weak: true)
      set text(size: zh(4))
      // Display the authors list.
      set par(leading: 0.6em)
      for i in range(calc.ceil(authors.len() / 3)) {
        let end = calc.min((i + 1) * 3, authors.len())
        let is-last = authors.len() == end
        let slice = authors.slice(i * 3, end)
        grid(
          columns: slice.len() * (1fr,),
          gutter: 12pt,
          ..slice.map(author => align(center, {
            text(author.name_cn)
            if "student_id" in author [
              \ #emph(author.student_id)
            ]
            if "department" in author [
              \ #emph(author.department)
            ]
            if "email" in author {
              if type(author.email) == str [
                \ #link("mailto:" + author.email)
              ] else [
                \ #author.email
              ]
            }
          }))
        )

        if not is-last {
          v(16pt, weak: true)
        }
      }
    }
  )
  set par(leading: 0.5em, spacing: 0.5em, first-line-indent: 2em, justify: true)
  place.flush()
  body
  place.flush()
  // 使用GB7714-15格式引用文献
  bilingual-bibliography(
    title: "参考文献",
    style: "gb-7714-2015-numeric.csl",
    bibliography: std.bibliography.with(bibliography),
  )
  v(1em)
  //作者信息

  for author in authors {
    if "author_description" in author [
      #strong(author.name_cn)#h(1em,weak: true) #author.author_description
    ]
  }
}

#let tlb_l() = {
  table.hline(stroke:2pt+black)
}
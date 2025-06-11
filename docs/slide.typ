#import "@preview/touying:0.6.1": *
#import "@preview/touying-buaa:0.2.0": *
#import "@preview/pinit:0.2.2": *
#import "@preview/cetz:0.3.4"
#import "@preview/lilaq:0.2.0" as lq
#import "bilingual-bibliography.typ": *
#import "authors.typ": *
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

import React from 'react';
import Link from '@docusaurus/Link';
import Layout from '@theme/Layout';
import courseHero from '@site/docs/course-preparation/images/part0-course-prep-hero-v2.png';
import styles from './index.module.css';

const stages = [
  {
    number: '00–05',
    title: '课程准备',
    description: '搭建 VMware、Ubuntu 24.04 与 AI Agent 工作环境。',
    href: '/docs/course-preparation',
    tone: 'purple',
  },
  {
    number: '04–10',
    title: 'V853 系统开发',
    description: '完成 SDK 配置编译、固件烧录与 LCD/Touch 验证。',
    href: '/docs/v853-tina-linux',
    tone: 'orange',
  },
  {
    number: '13–22',
    title: 'LVGL 9 移植',
    description: '接通显示、触摸、刷新循环、字体图片与 Tina SDK。',
    href: '/docs/lvgl9-porting',
    tone: 'teal',
  },
  {
    number: '23–31',
    title: 'EdgeOS V853 移植',
    description: '从核心桌面 UI 开始，适配 Settings 设置中心、横竖屏布局、触摸导航与 Tina 软件包。',
    href: '/docs/edgeos-v853-porting',
    tone: 'slate',
    status: '预览开放',
  },
  {
    number: 'NEXT',
    title: '多平台与进阶专题',
    description: 'V851s/V821/V861、Camera、MPP、NPU 与 OTA。',
    tone: 'slate',
  },
];

const outcomes = [
  ['环境可复现', '用检查表和日志固定开发环境，减少“换一台电脑就失效”的问题。'],
  ['过程可验证', '每一步都给出成功标志、验收标准和常见问题，而不是只看最终截图。'],
  ['AI 可控地参与', '先只读分析，再按边界授权，让 Agent 帮忙编译、排错与移植。'],
];

function StageCard({stage}) {
  const content = (
    <>
      <div className={styles.stageTopline}>
        <span className={`${styles.stageNumber} ${styles[stage.tone]}`}>
          {stage.number}
        </span>
        <span className={styles.stageStatus}>
          {stage.status ?? (stage.href ? '已开放' : '规划中')}
        </span>
      </div>
      <h3>{stage.title}</h3>
      <p>{stage.description}</p>
      {stage.href && <span className={styles.stageLink}>进入章节 →</span>}
    </>
  );

  return stage.href ? (
    <Link className={styles.stageCard} to={stage.href}>
      {content}
    </Link>
  ) : (
    <article className={`${styles.stageCard} ${styles.stageCardPlanned}`}>
      {content}
    </article>
  );
}

export default function Home() {
  return (
    <Layout
      title="课程首页"
      description="使用 AI Agent 完成 V853 系统开发与 LVGL 9 多平台桌面系统 UI 移植实战。">
      <header className={styles.hero}>
        <div className={`container ${styles.heroGrid}`}>
          <div className={styles.heroCopy}>
            <div className={styles.eyebrow}>
              <span className={styles.eyebrowDot} />
              100ASK · 工程实践课程
            </div>
            <h1>AI 辅助嵌入式 Linux 多平台桌面系统 UI 移植实战</h1>
            <p className={styles.heroLead}>
              从统一开发环境开始，走通 V853 系统构建与板级验证，
              再用 AI Agent 把 LVGL 9 的显示、触摸和界面资源真正落到开发板上。
            </p>
            <div className={styles.heroActions}>
              <Link
                className="button button--primary button--lg"
                to="/docs/course-preparation/vmware-workstation">
                从第 00 节开始
              </Link>
              <Link
                className="button button--secondary button--lg"
                to="/docs/">
                查看课程目录
              </Link>
            </div>
            <dl className={styles.heroStats}>
              <div>
                <dt>22</dt>
                <dd>篇课程文档</dd>
              </div>
              <div>
                <dt>79</dt>
                <dd>张流程图与截图</dd>
              </div>
              <div>
                <dt>4</dt>
                <dd>个已开放阶段</dd>
              </div>
            </dl>
          </div>

          <div className={styles.heroVisual}>
            <div className={styles.visualGlow} />
            <img
              src={courseHero}
              alt="Windows、Ubuntu、AI 开发工具与嵌入式开发板组成的课程实践路线"
            />
            <div className={`${styles.floatingLabel} ${styles.floatingLabelTop}`}>
              <span>01</span> 环境与工具
            </div>
            <div className={`${styles.floatingLabel} ${styles.floatingLabelBottom}`}>
              <span>03</span> LVGL 9 上板
            </div>
          </div>
        </div>
      </header>

      <main>
        <section className={styles.pathSection}>
          <div className="container">
            <div className={styles.sectionHeading}>
              <div>
                <span className={styles.sectionKicker}>LEARNING PATH</span>
                <h2>沿着一条可验证的工程路线学习</h2>
              </div>
              <p>每个阶段都以前一阶段的验收结果为输入，便于快速定位问题所在层级。</p>
            </div>
            <div className={styles.stageGrid}>
              {stages.map((stage) => (
                <StageCard key={stage.title} stage={stage} />
              ))}
            </div>
          </div>
        </section>

        <section className={styles.outcomesSection}>
          <div className={`container ${styles.outcomesGrid}`}>
            <div className={styles.outcomesIntro}>
              <span className={styles.sectionKicker}>COURSE METHOD</span>
              <h2>不只“做出来”，还要知道为什么成功</h2>
              <p>
                课程文档把操作、证据和故障边界放在同一页，适合边实践边记录，也方便与 AI Agent 协作排错。
              </p>
              <Link className={styles.textLink} to="/docs/">
                浏览完整课程总览 →
              </Link>
            </div>
            <div className={styles.outcomeList}>
              {outcomes.map(([title, description], index) => (
                <article className={styles.outcomeItem} key={title}>
                  <span>{String(index + 1).padStart(2, '0')}</span>
                  <div>
                    <h3>{title}</h3>
                    <p>{description}</p>
                  </div>
                </article>
              ))}
            </div>
          </div>
        </section>
      </main>
    </Layout>
  );
}

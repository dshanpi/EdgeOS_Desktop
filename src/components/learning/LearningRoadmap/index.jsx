import React, {useMemo} from 'react';
import Link from '@docusaurus/Link';
import useCourseProgress from '@site/src/hooks/useCourseProgress';
import {getLearningRoadmap} from '@site/src/data/learningRoadmaps';
import styles from './styles.module.css';

const stateMeta = {
  completed: {label: '已完成', icon: '✓'},
  acknowledged: {label: '缺口已知', icon: '✓'},
  current: {label: '当前学习', icon: '◉'},
  available: {label: '可学习', icon: '○'},
  optional: {label: '可选实践', icon: '◇'},
  locked: {label: '待前置', icon: '●'},
  warning: {label: '需注意', icon: '!'},
};

function isChapterSatisfied(item, completedSet, acknowledgedWarningSet) {
  return completedSet.has(item.id)
    || (item.incomplete && acknowledgedWarningSet.has(item.id));
}

function getChapterStates(chapters, completedSet, acknowledgedWarningSet) {
  const prerequisiteMet = (item) =>
    (item.prerequisites ?? []).every((id) => {
      const prerequisite = chapters.find((chapterItem) => chapterItem.id === id);
      return prerequisite
        ? isChapterSatisfied(prerequisite, completedSet, acknowledgedWarningSet)
        : false;
    });
  const firstActionable = chapters.find(
    (item) => !item.optional
      && !item.incomplete
      && !completedSet.has(item.id)
      && prerequisiteMet(item),
  );

  return Object.fromEntries(
    chapters.map((item) => {
      let state = 'locked';
      if (completedSet.has(item.id)) {
        state = 'completed';
      } else if (item.incomplete && acknowledgedWarningSet.has(item.id)) {
        state = 'acknowledged';
      } else if (prerequisiteMet(item)) {
        if (item.incomplete) state = 'warning';
        else if (item.optional) state = 'optional';
        else if (item.warning) state = 'warning';
        else if (item.id === firstActionable?.id) state = 'current';
        else state = 'available';
      }
      return [item.id, state];
    }),
  );
}

function getStageUnit(stageCount) {
  if (stageCount >= 8) return 100;
  if (stageCount >= 6) return 110;
  return 148;
}

function DependencyLines({chapters, states}) {
  const unit = getStageUnit(chapters.length);
  const width = chapters.length >= 6
    ? chapters.length * unit
    : Math.max(chapters.length * unit, 740);
  const positions = Object.fromEntries(
    chapters.map((item, index) => [item.id, index * unit + unit / 2]),
  );
  const edges = chapters.flatMap((item) =>
    (item.prerequisites ?? []).map((sourceId) => ({
      sourceId,
      targetId: item.id,
    })),
  );

  return (
    <svg
      className={styles.dependencySvg}
      viewBox={`0 0 ${width} 92`}
      preserveAspectRatio="none"
      aria-hidden="true"
      focusable="false">
      <defs>
        <marker id="roadmap-arrow-completed" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
          <path d="M 0 0 L 8 4 L 0 8 z" fill="#07883d" />
        </marker>
        <marker id="roadmap-arrow-active" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
          <path d="M 0 0 L 8 4 L 0 8 z" fill="#16a34a" />
        </marker>
        <marker id="roadmap-arrow-locked" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
          <path d="M 0 0 L 8 4 L 0 8 z" fill="#667085" />
        </marker>
      </defs>
      {edges.map(({sourceId, targetId}) => {
        const sourceX = positions[sourceId];
        const targetX = positions[targetId];
        const span = Math.abs(targetX - sourceX) / unit;
        const peak = Math.max(12, 64 - span * 8);
        const targetState = states[targetId];
        const lineState =
          targetState === 'completed' || targetState === 'acknowledged'
            ? 'completed'
            : targetState === 'locked'
              ? 'locked'
              : 'active';
        return (
          <path
            key={`${sourceId}-${targetId}`}
            className={`${styles.dependencyLine} ${styles[`line_${lineState}`]}`}
            d={`M ${sourceX} 84 C ${sourceX} ${peak}, ${targetX} ${peak}, ${targetX} 84`}
            markerEnd={`url(#roadmap-arrow-${lineState})`}
          />
        );
      })}
    </svg>
  );
}

function StateLegend() {
  return (
    <div className={styles.legend} aria-label="学习状态图例">
      {Object.entries(stateMeta).map(([state, meta]) => (
        <span key={state}>
          <i className={`${styles.legendIcon} ${styles[`state_${state}`]}`} aria-hidden="true">
            {meta.icon}
          </i>
          {meta.label}
        </span>
      ))}
      <span className={styles.dependencyLegend}>
        <i aria-hidden="true" /> 前置依赖
      </span>
    </div>
  );
}

function ChapterCard({chapter: item, state, canAct, onAcknowledge, onToggle}) {
  const meta = stateMeta[state];
  const actionDone = state === 'completed' || state === 'acknowledged';
  let buttonText = '等待前置章节';
  if (item.incomplete) {
    buttonText = state === 'acknowledged'
      ? '✓ 已确认资料缺口'
      : canAct
        ? '确认已了解资料缺口'
        : buttonText;
  } else if (state === 'completed') {
    buttonText = item.optional ? '✓ 可选实践已完成' : '✓ 已完成验收';
  } else if (canAct) {
    buttonText = item.optional ? '标记可选实践完成' : '标记完成验收';
  }

  return (
    <article className={`${styles.chapterCard} ${styles[`card_${state}`]}`}>
      <Link className={styles.chapterMain} to={item.href} aria-current={state === 'current' ? 'step' : undefined}>
        <div className={styles.chapterTopline}>
          <span className={styles.chapterNumber}>{item.number}</span>
          <span className={`${styles.stateBadge} ${styles[`state_${state}`]}`}>
            <b aria-hidden="true">{meta.icon}</b>
            {meta.label}
          </span>
        </div>
        <h3>{item.shortTitle}</h3>
        <p>{item.description}</p>
        <div className={styles.evidenceList} aria-label="建议学习证据">
          {item.evidence.slice(0, 2).map((entry) => (
            <span key={entry}>{entry}</span>
          ))}
        </div>
        {item.warning && <small className={styles.chapterWarning}>⚠ {item.warning}</small>}
      </Link>
      <button
        className={styles.completionButton}
        type="button"
        disabled={!canAct && !actionDone}
        aria-label={`${buttonText}：第 ${item.number} 节 ${item.title}`}
        aria-pressed={actionDone}
        title={!canAct && !actionDone ? '请先完成前置章节' : undefined}
        onClick={() => item.incomplete ? onAcknowledge(item.id) : onToggle(item.id)}>
        {buttonText}
      </button>
    </article>
  );
}

function ProgressPanel({
  course,
  chapters,
  states,
  completedSet,
  acknowledgedWarningSet,
  onReset,
  isReady,
  showSummary = false,
}) {
  const coreChapters = chapters.filter((item) => !item.optional && !item.incomplete);
  const optionalChapters = chapters.filter((item) => item.optional);
  const incompleteChapters = chapters.filter((item) => item.incomplete);
  const completedCount = coreChapters.filter((item) => completedSet.has(item.id)).length;
  const optionalCompletedCount = optionalChapters.filter((item) => completedSet.has(item.id)).length;
  const acknowledgedCount = incompleteChapters.filter((item) => acknowledgedWarningSet.has(item.id)).length;
  const total = coreChapters.length;
  const percent = total === 0 ? 0 : Math.round((completedCount / total) * 100);
  const coreComplete = completedCount === total;
  const actionable = chapters.find((item) =>
    ['current', 'warning', 'available', 'optional'].includes(states[item.id]),
  );
  const nextChapter = actionable ?? chapters.at(-1);
  const notStarted = total - completedCount - (coreComplete ? 0 : 1);
  const hasLocalProgress = completedSet.size > 0 || acknowledgedWarningSet.size > 0;
  const supplementaryLabel = optionalChapters.length > 0
    ? `可选 ${optionalCompletedCount} / ${optionalChapters.length}`
    : incompleteChapters.length > 0
      ? `缺口确认 ${acknowledgedCount} / ${incompleteChapters.length}`
      : `待开始 ${Math.max(0, notStarted)} 章`;

  return (
    <>
      {showSummary && (
        <summary className={styles.progressSummary}>
          <span>核心进度</span>
          <strong>{completedCount} / {total} 章 · {percent}%</strong>
        </summary>
      )}
      <div className={styles.progressContent}>
        <p className="sr-only" aria-live="polite">
          核心章节已完成 {completedCount} / {total}，进度 {percent}%
        </p>
        <h2>核心学习进度</h2>
        <div
          className={styles.progressRing}
          style={{'--roadmap-progress': `${percent * 3.6}deg`}}
          role="progressbar"
          aria-label="核心学习进度"
          aria-valuemin="0"
          aria-valuemax="100"
          aria-valuenow={percent}>
          <div>
            <strong>{completedCount} / {total}</strong>
            <span>章</span>
            <b>{percent}%</b>
          </div>
        </div>
        <dl className={styles.progressStats}>
          <div><dt>核心完成</dt><dd>{completedCount} 章</dd></div>
          <div><dt>学习中</dt><dd>{coreComplete ? 0 : 1} 章</dd></div>
          <div><dt>{supplementaryLabel.split(' ')[0]}</dt><dd>{supplementaryLabel.split(' ').slice(1).join(' ')}</dd></div>
        </dl>
        <div className={styles.currentStageCard}>
          <span>当前阶段</span>
          <strong>{coreComplete ? '核心路线已完成' : nextChapter.title}</strong>
          <small>{coreComplete ? course.outcome : nextChapter.outcome}</small>
          {coreComplete && optionalChapters.length > optionalCompletedCount && (
            <small>另有 {optionalChapters.length - optionalCompletedCount} 个可选工具实践，不影响核心完成。</small>
          )}
          {incompleteChapters.length > acknowledgedCount && (
            <small>仍有 {incompleteChapters.length - acknowledgedCount} 个源资料缺口需要阅读并确认。</small>
          )}
        </div>
        <Link className={styles.primaryAction} to={nextChapter.href}>
          <span aria-hidden="true">▶</span>
          {completedCount === 0
            ? '开始学习'
            : coreComplete && actionable
              ? '继续可选实践'
              : coreComplete
                ? '复习学习路线'
                : '继续学习'}
        </Link>
        <div className={styles.goalsCard}>
          <h3>学习目标</h3>
          <ul>
            {course.goals.map((goal) => <li key={goal}>{goal}</li>)}
          </ul>
        </div>
        <nav className={styles.resourcesCard} aria-label="学习资源">
          <h3>学习资源</h3>
          <Link to="/docs/">课程总览</Link>
          <Link to={chapters[0].href}>本路线第一章</Link>
        </nav>
        <div className={styles.tipCard}>
          <strong>学习提示</strong>
          <p>完成页面中的验收清单并保存证据后，再在路线图中标记完成。</p>
        </div>
        {isReady && hasLocalProgress && (
          <button className={styles.resetButton} type="button" onClick={onReset}>
            重置本机进度
          </button>
        )}
      </div>
    </>
  );
}

function ContextMap({course, chapterById}) {
  return (
    <section className={styles.contextPanel} aria-labelledby={`${course.id}-context-title`}>
      <div className={styles.panelHeading}>
        <div>
          <span>TECHNICAL CONTEXT</span>
          <h2 id={`${course.id}-context-title`}>{course.context.title}</h2>
        </div>
        <p>点击映射项进入对应章节</p>
      </div>
      <div className={styles.contextRows}>
        {course.context.rows.map((row, index) => (
          <div className={`${styles.contextRow} ${styles[`tone_${row.tone}`]}`} key={row.label}>
            <div className={styles.layerLabel}>
              <span className={styles.layerIcon} aria-hidden="true">{index + 1}</span>
              <div><strong>{row.label}</strong><small>{row.detail}</small></div>
            </div>
            <span className={styles.layerArrow} aria-hidden="true">→</span>
            <div className={styles.layerItems}>
              {row.items.map((item) => {
                const linkedChapter = chapterById[item.chapterId];
                return (
                  <Link key={`${row.label}-${item.label}`} to={linkedChapter.href}>
                    <small>第 {linkedChapter.number} 节</small>
                    <strong>{item.label}</strong>
                  </Link>
                );
              })}
            </div>
          </div>
        ))}
      </div>
      <p className={styles.learningPrinciple}>
        <span aria-hidden="true">💡</span> 学习原则：{course.context.principle}
      </p>
    </section>
  );
}

export default function LearningRoadmap({courseId}) {
  const course = getLearningRoadmap(courseId);
  if (!course) throw new Error(`Unknown learning roadmap: ${courseId}`);

  const {
    acknowledgedWarningSet,
    completedSet,
    isReady,
    resetProgress,
    toggleChapter,
    toggleWarningAcknowledgement,
  } = useCourseProgress(course);
  const states = useMemo(
    () => getChapterStates(course.chapters, completedSet, acknowledgedWarningSet),
    [acknowledgedWarningSet, completedSet, course.chapters],
  );
  const chapterById = useMemo(
    () => Object.fromEntries(course.chapters.map((item) => [item.id, item])),
    [course.chapters],
  );
  const currentChapter = course.chapters.find((item) => states[item.id] === 'current')
    ?? course.chapters.find((item) => states[item.id] === 'warning')
    ?? course.chapters.find((item) => states[item.id] === 'available')
    ?? course.chapters.find((item) => states[item.id] === 'optional')
    ?? course.chapters.at(-1);
  const coreChapters = course.chapters.filter((item) => !item.optional && !item.incomplete);
  const routeComplete = coreChapters.every((item) => completedSet.has(item.id));
  const optionalRemaining = course.chapters.filter(
    (item) => item.optional && !completedSet.has(item.id),
  ).length;
  const acknowledgedGaps = course.chapters.filter(
    (item) => item.incomplete && acknowledgedWarningSet.has(item.id),
  ).length;

  return (
    <div className={`${styles.roadmap} learning-roadmap-page`}>
      <header className={styles.routeHeader}>
        <span className={styles.partLabel}>{course.part} · LEARNING ROADMAP</span>
        <h1>{course.title}</h1>
        <p>{course.subtitle} · {course.chapters.length} 章循序推进</p>
        <div className={styles.identityLine}>
          <span>{course.sourceTitle}</span>
          <span>{course.platform}</span>
          <span>{course.version}</span>
        </div>
      </header>

      <noscript>
        <p className={styles.noScriptNotice}>
          进度记录需要 JavaScript；下面的全部章节链接仍可正常打开和学习。
        </p>
      </noscript>

      <div className={styles.workspaceGrid}>
        <div className={styles.mainWorkspace}>
          <section className={styles.mapPanel} aria-labelledby={`${course.id}-map-title`}>
            <div className={styles.panelHeading}>
              <div>
                <span>COURSE PATH</span>
                <h2 id={`${course.id}-map-title`}>学习路线总图</h2>
              </div>
              <p>依赖线表示建议前置关系，文档链接始终可以预览。</p>
            </div>
            <div className={styles.mapViewport}>
              <div
                className={styles.mapInner}
                style={{
                  '--stage-count': course.chapters.length,
                  '--stage-width': `${getStageUnit(course.chapters.length)}px`,
                  '--roadmap-width': course.chapters.length >= 6
                    ? `${course.chapters.length * getStageUnit(course.chapters.length)}px`
                    : '740px',
                }}>
                <DependencyLines chapters={course.chapters} states={states} />
                <div className={styles.chapterGrid}>
                  {course.chapters.map((item) => {
                    const canAct = (item.prerequisites ?? []).every((id) => {
                      const prerequisite = chapterById[id];
                      return prerequisite
                        ? isChapterSatisfied(prerequisite, completedSet, acknowledgedWarningSet)
                        : false;
                    });
                    return (
                      <ChapterCard
                        key={item.id}
                        chapter={item}
                        state={states[item.id]}
                        canAct={canAct}
                        onAcknowledge={toggleWarningAcknowledgement}
                        onToggle={toggleChapter}
                      />
                    );
                  })}
                </div>
              </div>
            </div>
            <StateLegend />
          </section>

          <div className={styles.currentBand}>
            <span>{routeComplete ? '核心路线状态' : '当前建议学习'}</span>
            <strong>
              {routeComplete
                ? `已完成 ${coreChapters.length} 个核心验收${optionalRemaining > 0 ? ` · ${optionalRemaining} 个可选实践待探索` : ''}${acknowledgedGaps > 0 ? ` · ${acknowledgedGaps} 个资料缺口已确认` : ''}`
                : `第 ${currentChapter.number} 节 · ${currentChapter.title}`}
            </strong>
            <Link to={currentChapter.href}>
              {routeComplete && optionalRemaining > 0 ? '进入可选实践' : routeComplete ? '回顾最终章节' : '进入章节'} →
            </Link>
          </div>

          <ContextMap course={course} chapterById={chapterById} />

          <section className={styles.courseFacts} aria-label="课程路线信息">
            <div><span>课程部分</span><strong>{course.part}</strong></div>
            <div><span>目标平台</span><strong>{course.platform}</strong></div>
            <div><span>章节总数</span><strong>{course.chapters.length} 章</strong></div>
            <div><span>完成结果</span><strong>{course.outcome}</strong></div>
          </section>
        </div>

        <aside className={`${styles.progressRail} ${styles.desktopProgress}`}>
          <ProgressPanel
            course={course}
            chapters={course.chapters}
            states={states}
            completedSet={completedSet}
            acknowledgedWarningSet={acknowledgedWarningSet}
            onReset={resetProgress}
            isReady={isReady}
          />
        </aside>

        <details className={`${styles.progressRail} ${styles.progressDrawer}`}>
          <ProgressPanel
            course={course}
            chapters={course.chapters}
            states={states}
            completedSet={completedSet}
            acknowledgedWarningSet={acknowledgedWarningSet}
            onReset={resetProgress}
            isReady={isReady}
            showSummary
          />
        </details>
      </div>
    </div>
  );
}

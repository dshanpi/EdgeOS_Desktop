import {useCallback, useEffect, useMemo, useState} from 'react';

const SCHEMA_VERSION = 1;

const emptyProgress = () => ({
  acknowledgedWarningIds: [],
  completedIds: [],
});

export default function useCourseProgress(course) {
  const storageKey = `100ask:learning:${course.id}:v${SCHEMA_VERSION}`;
  const completableIds = useMemo(
    () => new Set(course.chapters.filter((item) => !item.incomplete).map((item) => item.id)),
    [course.chapters],
  );
  const incompleteIds = useMemo(
    () => new Set(course.chapters.filter((item) => item.incomplete).map((item) => item.id)),
    [course.chapters],
  );
  const [progress, setProgress] = useState(emptyProgress);
  const [isReady, setIsReady] = useState(false);

  useEffect(() => {
    try {
      const rawValue = window.localStorage.getItem(storageKey);
      const stored = rawValue ? JSON.parse(rawValue) : null;
      if (stored?.version === SCHEMA_VERSION) {
        setProgress({
          acknowledgedWarningIds: Array.isArray(stored.acknowledgedWarnings)
            ? stored.acknowledgedWarnings.filter((id) => incompleteIds.has(id))
            : [],
          completedIds: Array.isArray(stored.completed)
            ? stored.completed.filter((id) => completableIds.has(id))
            : [],
        });
      }
    } catch {
      setProgress(emptyProgress());
    } finally {
      setIsReady(true);
    }
  }, [completableIds, incompleteIds, storageKey]);

  const persist = useCallback(
    (nextProgress) => {
      try {
        window.localStorage.setItem(
          storageKey,
          JSON.stringify({
            version: SCHEMA_VERSION,
            completed: nextProgress.completedIds,
            acknowledgedWarnings: nextProgress.acknowledgedWarningIds,
          }),
        );
      } catch {
        // The roadmap remains usable when storage is unavailable.
      }
    },
    [storageKey],
  );

  const toggleChapter = useCallback(
    (chapterId) => {
      if (!completableIds.has(chapterId)) return;
      setProgress((current) => {
        const completedIds = current.completedIds.includes(chapterId)
          ? current.completedIds.filter((id) => id !== chapterId)
          : [...current.completedIds, chapterId];
        const next = {...current, completedIds};
        persist(next);
        return next;
      });
    },
    [completableIds, persist],
  );

  const toggleWarningAcknowledgement = useCallback(
    (chapterId) => {
      if (!incompleteIds.has(chapterId)) return;
      setProgress((current) => {
        const acknowledgedWarningIds = current.acknowledgedWarningIds.includes(chapterId)
          ? current.acknowledgedWarningIds.filter((id) => id !== chapterId)
          : [...current.acknowledgedWarningIds, chapterId];
        const next = {...current, acknowledgedWarningIds};
        persist(next);
        return next;
      });
    },
    [incompleteIds, persist],
  );

  const resetProgress = useCallback(() => {
    const next = emptyProgress();
    setProgress(next);
    persist(next);
  }, [persist]);

  return {
    acknowledgedWarningSet: useMemo(
      () => new Set(progress.acknowledgedWarningIds),
      [progress.acknowledgedWarningIds],
    ),
    completedSet: useMemo(() => new Set(progress.completedIds), [progress.completedIds]),
    isReady,
    resetProgress,
    toggleChapter,
    toggleWarningAcknowledgement,
  };
}

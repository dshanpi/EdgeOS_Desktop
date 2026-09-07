// @ts-check

/** @type {import('@docusaurus/plugin-content-docs').SidebarsConfig} */
const sidebars = {
  courseSidebar: [
    {
      type: 'doc',
      id: 'overview',
      label: '课程总览',
    },
    {
      type: 'category',
      label: 'Part 0 · 课程准备',
      collapsed: false,
      link: {
        type: 'doc',
        id: 'course-preparation/index',
      },
      items: [
        'course-preparation/vmware-workstation',
        'course-preparation/ubuntu-24-vm',
        'course-preparation/vm-base-setup',
        'course-preparation/claude-code',
        'course-preparation/codex-cli',
        'course-preparation/deepseek-harness',
      ],
    },
    {
      type: 'category',
      label: 'Part 1 · V853 系统开发',
      collapsed: true,
      link: {
        type: 'doc',
        id: 'v853-tina-linux/index',
      },
      items: [
        'v853-tina-linux/ubuntu-environment',
        'v853-tina-linux/tina-sdk-overview',
        'v853-tina-linux/v853-sdk-build',
        'v853-tina-linux/firmware-flashing',
        'v853-tina-linux/peripheral-validation',
      ],
    },
    {
      type: 'category',
      label: 'Part 2 · LVGL 9 移植',
      collapsed: true,
      link: {
        type: 'doc',
        id: 'lvgl9-porting/index',
      },
      items: [
        'lvgl9-porting/pin-lvgl9-source',
        'lvgl9-porting/ai-build-lvgl9-project',
        'lvgl9-porting/lvgl9-on-lcd',
        'lvgl9-porting/touch-input',
        'lvgl9-porting/refresh-loop',
        'lvgl9-porting/fonts-and-images',
        'lvgl9-porting/performance-and-stability',
        'lvgl9-porting/integrate-with-tina-sdk',
      ],
    },
    {
      type: 'category',
      label: 'Part 3 · EdgeOS V853 移植（预览）',
      collapsed: true,
      link: {
        type: 'doc',
        id: 'edgeos-v853-porting/index',
      },
      items: [
        'edgeos-v853-porting/edgeos-desktop-ui',
        'edgeos-v853-porting/edgeos-settings',
        'edgeos-v853-porting/edgeos-settings-services',
      ],
    },
  ],
};

export default sidebars;

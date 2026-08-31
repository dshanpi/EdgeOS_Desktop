// @ts-check
// `@type` JSDoc annotations allow editor autocompletion and type checking
// (when paired with `@ts-check`).
// There are various equivalent ways to declare your Docusaurus config.
// See: https://docusaurus.io/docs/api/docusaurus-config

import { themes as prismThemes } from 'prism-react-renderer';

// This runs in Node.js - Don't use client-side code here (browser APIs, JSX...)

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'AI 辅助嵌入式 Linux UI 移植实战',
  tagline: '从 Tina Linux 到 LVGL 9 的多平台桌面系统 UI 移植课程',
  favicon: 'img/course-logo.svg',
  titleDelimiter: '·',

  // Future flags, see https://docusaurus.io/docs/api/docusaurus-config#future
  future: {
    v4: true, // Improve compatibility with the upcoming Docusaurus v4
  },

  url: 'https://edgeos.100ask.net',
  // Set the /<baseUrl>/ pathname under which your site is served
  // For GitHub pages deployment, it is often '/<projectName>/'
  baseUrl: '/',

  // GitHub pages deployment config.
  // If you aren't using GitHub pages, you don't need these.
  organizationName: 'dshanpi', // Usually your GitHub org/user name.
  projectName: 'EdgeOS_Desktop', // Usually your repo name.

  onBrokenLinks: 'throw',

  // Even if you don't use internationalization, you can use this field to set
  // useful metadata like html lang. For example, if your site is Chinese, you
  // may want to replace "en" with "zh-Hans".
  i18n: {
    defaultLocale: 'zh-Hans',
    locales: ['zh-Hans'],
  },
  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          sidebarPath: './sidebars.js',
          // Please change this to your repo.
          // Remove this to remove the "edit this page" links.
          editUrl:
            'https://github.com/dshanpi/EdgeOS_Desktop/tree/docs-site/',
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      }),
    ],
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      colorMode: {
        respectPrefersColorScheme: true,
      },
      docs: {
        sidebar: {
          autoCollapseCategories: true,
          hideable: true,
        },
      },
      navbar: {
        title: 'AI Agent · 嵌入式 Linux UI 实战',
        hideOnScroll: true,
        logo: {
          alt: 'AI 辅助嵌入式 Linux UI 移植实战',
          src: 'img/course-logo.svg',
        },
        items: [
          {
            type: 'docSidebar',
            sidebarId: 'courseSidebar',
            label: '课程文档',
            position: 'left',
          },
          {to: '/docs/course-preparation', label: '课程准备', position: 'left'},
          {to: '/docs/v853-tina-linux', label: 'V853 系统开发', position: 'left'},
          {to: '/docs/lvgl9-porting', label: 'LVGL 9 移植', position: 'left'},
          {
            href: 'https://github.com/dshanpi/EdgeOS_Desktop/tree/docs-site',
            label: 'GitHub',
            position: 'right',
          },
        ],
      },
      footer: {
        style: 'dark',
        links: [
          {
            title: '课程导航',
            items: [
              {label: '课程总览', to: '/docs/'},
              {label: '课程准备', to: '/docs/course-preparation'},
              {label: 'V853 系统开发', to: '/docs/v853-tina-linux'},
              {label: 'LVGL 9 移植', to: '/docs/lvgl9-porting'},
            ],
          },
          {
            title: '项目',
            items: [
              {
                label: 'GitHub',
                href: 'https://github.com/dshanpi/EdgeOS_Desktop/tree/docs-site',
              },
            ],
          },
        ],
        copyright: `Copyright © ${new Date().getFullYear()} 100askTeam. Built with Docusaurus.`,
      },
      prism: {
        theme: prismThemes.github,
        darkTheme: prismThemes.dracula,
      },
    }),

  // Add the Mermaid plugin and enable it in markdown
  markdown: {
    mermaid: true,
    // The source content uses MDX1-style features (HTML comments such as the
    // blog `<!-- truncate -->` marker, `::: admonitions`, and explicit heading
    // ids like `{#my-id}`). With future.v4: true, Docusaurus 3.10 disables MDX1
    // compatibility by default; keep it enabled so all existing docs/blog build.
    mdx1Compat: {
      comments: true,
      admonitions: true,
      headingIds: true,
    },
    hooks: {
      onBrokenMarkdownLinks: 'throw',
      onBrokenMarkdownImages: 'throw',
    },
  },

  themes: ['@docusaurus/theme-mermaid'],

  plugins: [
    [
      "@easyops-cn/docusaurus-search-local",
      {
        hashed: true,
        indexBlog: false,
        language: ["en", "zh"],
      },
    ],
  ],
};

export default config;

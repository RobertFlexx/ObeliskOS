This directory is intended to be published as a static opkg repository.

Layout:
  index.json
  packages/*.opk

Quick flow:
  1) Build packages into opkg/repo-site/packages/
  2) Rebuild index:
       ./opkg/opkg repo index opkg/repo-site
  3) Commit and push to main
  4) GitHub Pages workflow publishes this directory

Then configure clients:
  /etc/opkg/repos.conf
    main https://<user>.github.io/<repo>

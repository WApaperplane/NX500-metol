# EYE28 offline development review

This is an offline development review package, not an installable package. Deployment is hard disabled. The unchanged upstream algorithm is from commit eafc274e0ef0b6cb95b5ad958b2f1ac86dfc3562; the library and license are hash pinned by the packager. The historical App, T2, libcapture and libface hashes in runtime/profile.json are references, not measurements of the restored camera.

Native integration is exercised against fake-root fixtures; those tests do not establish camera behavior. A trusted image proof producer and its actual connection to the snapshot remain to be implemented and reviewed. The restored camera's current version and dependencies must be verified. Real-device gates remain for fast startup, AF, half press, full press and low light, with recovery planning.

The original algorithm returns -1 when dlsym RTLD_NEXT face lookup fails; fallback cannot be guaranteed for every failure. No installation, device writer, service or automatic startup entry is included. Historical upstream installation instructions must not be used.

The packager uses a strict allowlist and rejects symlinks and pinned binary or license changes. The pinned ARM helper was reviewed only for this production-disabled offline package; that review does not authorize device deployment. Existing earlier archives are obsolete.

The archive includes runtime C and header sources and the original replay sources. Native safety tests currently require the repository layout and are not included as independently runnable archive tests. From the repository root, run `python3 -m unittest discover -s test_server/eye28 -v` on a suitable development host.

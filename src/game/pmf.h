#ifndef AM2_PMF_H
#define AM2_PMF_H

/* The address of a non-virtual member function, as a plain code pointer.
 *
 * The reconstruction models the original's thiscall members as real C++
 * methods, so the hosted MSVC 6 recompiles them to the exact thiscall bytes the
 * game shipped (see src/vc6/README.md).  `patch_replace` installs a
 * reconstruction over its original by entry address, but a pointer-to-member is
 * not a code pointer.  For a single-inheritance, non-virtual method the PMF's
 * first machine word IS the entry address -- verified equal to the symbol on
 * both GCC i386 and MSVC 6 -- so read it back through a union.
 *
 * This never runs under MSVC 6 (the byte-match only compiles); it just has to
 * compile there, which it does (single-inheritance PMF is one word).
 */
template <class M>
static inline const void *pmf_addr(M pm)
{
    union { M pm; const void *p; } u;
    u.pm = pm;
    return u.p;
}

#endif /* AM2_PMF_H */

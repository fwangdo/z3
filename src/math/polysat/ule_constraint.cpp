/*++
Copyright (c) 2021 Microsoft Corporation

Module Name:

    polysat unsigned <= constraints

Author:

    Nikolaj Bjorner (nbjorner) 2021-03-19
    Jakob Rath 2021-04-6

Notes:

   Rewrite rules to simplify expressions.
   In the following let k, k1, k2 be values.
  
   - k1 <= k2   ==> 0 <= 0 if k1 <= k2
   - k1 <= k2   ==> 1 <= 0 if k1 >  k2
   - 0 <= p     ==> 0 <= 0
   - p <= -1    ==> 0 <= 0
   - k*2^n*p <= 0   ==> 2^n*p <= 0 if k is odd, leading coeffient is always a power of 2.
   - k <= p     ==> p - k <= - k - 1

TODO: clause simplifications:

   - p + k <= p  ==> p + k <= k & p != 0   for k != 0  
   - p*q = 0     ==> p = 0 or q = 0        applies to any factoring
   - 2*p <= 2*q  ==> (p >= 2^n-1 & q < 2^n-1) or (p >= 2^n-1 = q >= 2^n-1 & p <= q)
                 ==> (p >= 2^n-1 => q < 2^n-1 or p <= q) &
                     (p < 2^n-1 => p <= q) &
                     (p < 2^n-1 => q < 2^n-1)

   - 3*p <= 3*q ==> ?

Note:
     case p <= p + k is already covered because we test (lhs - rhs).is_val()
      
     It can be seen as an instance of lemma 5.2 of Supratik and John.

--*/

#include "math/polysat/constraint.h"
#include "math/polysat/solver.h"
#include "math/polysat/log.h"

namespace polysat {

    ule_constraint::ule_constraint(constraint_manager& m, pdd const& l, pdd const& r) :
        constraint(m, ckind_t::ule_t), m_lhs(l), m_rhs(r) {

        simplify();

        m_vars.append(m_lhs.free_vars());
        for (auto v : m_rhs.free_vars())
            if (!m_vars.contains(v))
                m_vars.push_back(v);

    }

    void ule_constraint::simplify() {
        if (m_lhs.is_zero()) {
            m_rhs = 0;
            return;
        }
        if (m_rhs.is_val() && m_rhs.val() == m_rhs.manager().max_value()) {
            m_lhs = 0, m_rhs = 0;
            return;
        }
        if (m_lhs == m_rhs) {
            m_lhs = m_rhs = 0;
            return;
        }
        if (m_lhs.is_val() && m_rhs.is_val()) {
            if (m_lhs.val() <= m_rhs.val())
                m_lhs = m_rhs = 0;
            else
                m_lhs = 1, m_rhs = 0;
            return;
        }
        // k <= p   -->   p - k <= - k - 1
        if (m_lhs.is_val()) {
            pdd k = m_lhs;
            m_lhs = m_rhs - k;
            m_rhs = - k - 1;
        }
        // a*p + q <= 0 <=> p + a^-1*q <= 0 for a odd
        if (m_rhs.is_zero() && !m_lhs.leading_coefficient().is_power_of_two()) {
            rational lc = m_lhs.leading_coefficient();
            rational x, y;
            gcd(lc, m_lhs.manager().two_to_N(), x, y);
            if (x.is_neg())
                x = mod(x, m_lhs.manager().two_to_N());
            m_lhs *= x;
            SASSERT(m_lhs.leading_coefficient().is_power_of_two());
        }
    }

    std::ostream& ule_constraint::display(std::ostream& out, lbool status) const {
        out << m_lhs;
        if (is_eq() && status == l_true) out << " == ";
        else if (is_eq() && status == l_false) out << " != ";
        else if (status == l_true) out << " <= ";
        else if (status == l_false) out << " > ";
        else out << " <=/> ";
        return out << m_rhs;
    }

    std::ostream& ule_constraint::display(std::ostream& out) const {
        return out << m_lhs << (is_eq() ? " == " : " <= ") << m_rhs;
    }

    void ule_constraint::narrow(solver& s, bool is_positive, bool first) {
        auto p = s.subst(lhs());
        auto q = s.subst(rhs());
        
        signed_constraint sc(this, is_positive);

        LOG_H3("Narrowing " << sc);
        LOG_V("Assignment: " << assignments_pp(s));
        LOG_V("Substituted LHS: " << lhs() << " := " << p);
        LOG_V("Substituted RHS: " << rhs() << " := " << q);

        if (is_always_false(is_positive, p, q)) {
            s.set_conflict(sc);
            return;
        }
        if (p.is_val() && q.is_val()) {
            SASSERT(!is_positive || p.val() <= q.val());
            SASSERT(is_positive || p.val() > q.val());
            return;
        }

        s.m_viable.intersect(p, q, sc);
    }

    bool ule_constraint::is_always_false(bool is_positive, pdd const& lhs, pdd const& rhs) {
        if (is_positive) {
            // lhs <= rhs
            if (rhs.is_zero())
                return lhs.is_never_zero();  // p <= 0 implies p == 0
            return lhs.is_val() && rhs.is_val() && lhs.val() > rhs.val();
        }
        else {
            // lhs > rhs
            if (lhs.is_zero())
                return true;  // 0 > ... is always false
            if (lhs == rhs)
                return true;  // p > p
            if (lhs.is_one() && rhs.is_never_zero())
                return true;  // 1 > p implies p == 0
            return lhs.is_val() && rhs.is_val() && lhs.val() <= rhs.val();
        }
    }

    bool ule_constraint::is_always_false(bool is_positive) const {
        return is_always_false(is_positive, lhs(), rhs());
    }

    bool ule_constraint::is_currently_false(solver& s, bool is_positive) const {
        auto p = s.subst(lhs());
        auto q = s.subst(rhs());
        return is_always_false(is_positive, p, q);
    }

    bool ule_constraint::is_currently_false(solver& s, assignment_t const& sub, bool is_positive) const { 
        auto p = s.subst(sub, lhs());
        auto q = s.subst(sub, rhs());
        return is_always_false(is_positive, p, q);
    }

    bool ule_constraint::is_currently_true(solver& s, assignment_t const& sub, bool is_positive) const { 
        return is_currently_false(s, sub, !is_positive);
    }

    bool ule_constraint::is_currently_true(solver& s, bool is_positive) const {
        auto p = s.subst(lhs());
        auto q = s.subst(rhs());
        if (is_positive) {
            if (p.is_zero())
                return true;
            return p.is_val() && q.is_val() && p.val() <= q.val();
        }
        else 
            return p.is_val() && q.is_val() && p.val() > q.val();
    }


    inequality ule_constraint::as_inequality(bool is_positive) const {
        if (is_positive)
            return inequality(lhs(), rhs(), false, this);
        else 
            return inequality(rhs(), lhs(), true, this);
    }

    unsigned ule_constraint::hash() const {
    	return mk_mix(lhs().hash(), rhs().hash(), kind());
    }
    
    bool ule_constraint::operator==(constraint const& other) const {
        return other.is_ule() && lhs() == other.to_ule().lhs() && rhs() == other.to_ule().rhs();
    }

    void ule_constraint::add_to_univariate_solver(solver& s, univariate_solver& us, unsigned dep, bool is_positive) const {
        auto p_coeff = s.subst(lhs()).get_univariate_coefficients();
        auto q_coeff = s.subst(rhs()).get_univariate_coefficients();
        us.add_ule(p_coeff, q_coeff, !is_positive, dep);
    }
}

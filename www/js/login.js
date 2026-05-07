









'use strict';

document.getElementById('login-form').addEventListener('submit', async function(e) {
  e.preventDefault();

  const btn = document.getElementById('login-btn');
  const errEl = document.getElementById('login-error');
  const username = document.getElementById('username').value.trim();
  const password = document.getElementById('password').value;
  const rememberMe = !!document.getElementById('remember-me')?.checked;

  btn.disabled = true;
  btn.textContent = 'Signing in...';
  errEl.style.display = 'none';

  try {
    const resp = await fetch('/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password, remember_me: rememberMe }),
    });
    const data = await resp.json();

    if (resp.ok) {
      if (data.force_password_reset) {
        sessionStorage.setItem('force_password_reset', 'true');
        window.location.href = '/change-password';
      } else {
        window.location.href = '/';
      }
    } else {
      errEl.textContent = data.error || 'Login failed.';
      errEl.style.display = 'block';
      btn.disabled = false;
      btn.textContent = 'Sign In';
    }
  } catch (err) {
    errEl.textContent = 'Network error - is the server running?';
    errEl.style.display = 'block';
    btn.disabled = false;
    btn.textContent = 'Sign In';
  }
});










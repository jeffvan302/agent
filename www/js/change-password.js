









'use strict';

let forced = sessionStorage.getItem('force_password_reset') === 'true';
if (forced) {
  document.getElementById('current-group').style.display = 'none';
  document.getElementById('subtitle').textContent =
    'Please set a new password before continuing.';
  sessionStorage.removeItem('force_password_reset');
}

document.getElementById('cp-form').addEventListener('submit', async function(e) {
  e.preventDefault();

  const btn = document.getElementById('cp-btn');
  const errEl = document.getElementById('cp-error');
  const current = document.getElementById('current-password').value;
  const newPw = document.getElementById('new-password').value;
  const confirm = document.getElementById('confirm-password').value;

  errEl.style.display = 'none';

  if (newPw.length < 10) {
    errEl.textContent = 'Password must be at least 10 characters.';
    errEl.style.display = 'block';
    return;
  }
  if (newPw !== confirm) {
    errEl.textContent = 'Passwords do not match.';
    errEl.style.display = 'block';
    return;
  }

  btn.disabled = true;
  btn.textContent = 'Updating...';

  try {
    const body = { new_password: newPw };
    if (!forced) body.current_password = current;

    const resp = await fetch('/api/change-password', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      credentials: 'same-origin',
      body: JSON.stringify(body),
    });
    const data = await resp.json();

    if (resp.ok) {
      window.location.href = '/';
    } else {
      errEl.textContent = data.error || 'Failed to update password.';
      errEl.style.display = 'block';
      btn.disabled = false;
      btn.textContent = 'Update Password';
    }
  } catch (err) {
    errEl.textContent = 'Network error - please try again.';
    errEl.style.display = 'block';
    btn.disabled = false;
    btn.textContent = 'Update Password';
  }
});










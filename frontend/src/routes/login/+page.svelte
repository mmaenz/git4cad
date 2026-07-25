<script lang="ts">
	import { authStore } from '$lib/stores/auth';
	import { register } from '$lib/api';
	import { goto } from '$app/navigation';
	import { page } from '$app/stores';

	let mode = $derived($page.url.searchParams.get('mode') === 'register' ? 'register' : 'login');

	let username = $state('');
	let password = $state('');
	let confirmPassword = $state('');
	let loading = $state(false);
	let error = $state('');
	let success = $state('');
	let newToken = $state('');
	let tokenCopied = $state(false);

	async function handleLogin() {
		if (!username.trim() || !password) {
			error = 'Please enter your username and password.';
			return;
		}
		loading = true;
		error = '';
		try {
			await authStore.login(username.trim(), password);
			goto('/');
		} catch (e: any) {
			error = e.message || 'Login failed. Check your credentials.';
		} finally {
			loading = false;
		}
	}

	async function handleRegister() {
		if (!username.trim() || !password) {
			error = 'Please fill in all fields.';
			return;
		}
		if (password !== confirmPassword) {
			error = 'Passwords do not match.';
			return;
		}
		if (username.trim().length < 2) {
			error = 'Username must be at least 2 characters.';
			return;
		}
		loading = true;
		error = '';
		try {
			const result = await register(username.trim(), password);
			authStore.setFromToken(result.username, result.token);
			newToken = result.token;
		} catch (e: any) {
			error = e.message || 'Registration failed.';
		} finally {
			loading = false;
		}
	}

	function handleSubmit(e: SubmitEvent) {
		e.preventDefault();
		if (mode === 'login') {
			handleLogin();
		} else {
			handleRegister();
		}
	}

	function switchMode(newMode: 'login' | 'register') {
		error = '';
		success = '';
		goto(`/login${newMode === 'register' ? '?mode=register' : ''}`, { replaceState: true });
	}
</script>

<svelte:head>
	<title>{mode === 'login' ? 'Sign in' : 'Create account'} - git4cad</title>
</svelte:head>

<div class="auth-page">
	<div class="auth-box">
		<div class="auth-header">
			<a href="/" class="auth-logo">
				<svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<path d="M3 3h18v18H3z"/>
					<path d="M9 3v18"/>
					<path d="M3 9h6"/>
					<path d="M3 15h6"/>
				</svg>
			</a>
			<h1>{mode === 'login' ? 'Sign in to git4cad' : 'Create your account'}</h1>
		</div>

		{#if newToken}
			<div class="token-reveal">
				<p class="token-intro">
					Account created! Save your git token — it won't be shown again.
				</p>
				<div class="token-box">
					<code class="token-value">{newToken}</code>
					<button
						class="btn btn-sm"
						onclick={() => { navigator.clipboard.writeText(newToken); tokenCopied = true; }}
					>
						{tokenCopied ? '✓ Copied' : 'Copy'}
					</button>
				</div>
				<p class="token-hint">
					Use it as your password for git:<br/>
					<code>git clone http://{$authStore.username}:TOKEN@localhost/…</code>
				</p>
				<button class="btn btn-primary token-continue" onclick={() => goto('/')}>
					Continue to dashboard
				</button>
			</div>
		{:else}
		<div class="auth-form-box">
			{#if error}
				<div class="alert alert-error">{error}</div>
			{/if}

			<form onsubmit={handleSubmit}>
				<div class="form-group">
					<label for="username">Username</label>
					<input
						id="username"
						type="text"
						autocomplete="username"
						spellcheck="false"
						autocapitalize="off"
						bind:value={username}
						required
					/>
				</div>

				<div class="form-group">
					<label for="password">Password</label>
					<input
						id="password"
						type="password"
						autocomplete={mode === 'login' ? 'current-password' : 'new-password'}
						bind:value={password}
						required
					/>
				</div>

				{#if mode === 'register'}
					<div class="form-group">
						<label for="confirm-password">Confirm password</label>
						<input
							id="confirm-password"
							type="password"
							autocomplete="new-password"
							bind:value={confirmPassword}
							required
						/>
					</div>
				{/if}

				<button type="submit" class="btn btn-primary submit-btn" disabled={loading}>
					{#if loading}
						<span class="spinner spinner-sm"></span>
					{/if}
					{mode === 'login' ? 'Sign in' : 'Create account'}
				</button>
			</form>
		</div>

		<div class="auth-switch">
			{#if mode === 'login'}
				<p>New to git4cad? <button class="link-btn" onclick={() => switchMode('register')}>Create an account</button></p>
			{:else}
				<p>Already have an account? <button class="link-btn" onclick={() => switchMode('login')}>Sign in</button></p>
			{/if}
		</div>
		{/if}
	</div>
</div>

<style>
	.auth-page {
		display: flex;
		align-items: center;
		justify-content: center;
		min-height: calc(100vh - 48px);
		padding: 24px 16px;
	}

	.auth-box {
		width: 100%;
		max-width: 340px;
	}

	.auth-header {
		text-align: center;
		margin-bottom: 16px;
	}

	.auth-logo {
		display: inline-flex;
		color: var(--color-accent);
		margin-bottom: 12px;
	}

	.auth-logo:hover {
		opacity: 0.8;
	}

	.auth-header h1 {
		font-size: 20px;
		font-weight: 300;
		color: var(--color-text);
	}

	.auth-form-box {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		padding: 20px;
		margin-bottom: 16px;
	}

	.submit-btn {
		width: 100%;
		margin-top: 4px;
		padding: 8px 12px;
		font-size: 14px;
		gap: 8px;
	}

	.spinner-sm {
		width: 14px;
		height: 14px;
		border-width: 2px;
	}

	.auth-switch {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		padding: 16px;
		text-align: center;
		font-size: 14px;
		color: var(--color-text-muted);
	}

	.link-btn {
		background: none;
		border: none;
		color: var(--color-accent);
		cursor: pointer;
		font-size: 14px;
		padding: 0;
		font-family: inherit;
	}

	.link-btn:hover {
		color: var(--color-accent-hover);
		text-decoration: underline;
	}

	.token-reveal {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		padding: 24px 20px;
		display: flex;
		flex-direction: column;
		gap: 16px;
	}

	.token-intro {
		font-size: 14px;
		font-weight: 600;
		color: var(--color-text);
		margin: 0;
	}

	.token-box {
		display: flex;
		align-items: center;
		gap: 8px;
		background-color: var(--color-bg);
		border: 1px solid var(--color-border);
		border-radius: var(--radius-sm);
		padding: 10px 12px;
	}

	.token-value {
		flex: 1;
		font-family: 'SFMono-Regular', Consolas, monospace;
		font-size: 12px;
		color: var(--color-text);
		word-break: break-all;
		user-select: all;
	}

	.btn-sm {
		padding: 4px 10px;
		font-size: 12px;
		white-space: nowrap;
		flex-shrink: 0;
	}

	.token-hint {
		font-size: 12px;
		color: var(--color-text-muted);
		margin: 0;
		line-height: 1.6;
	}

	.token-hint code {
		font-family: 'SFMono-Regular', Consolas, monospace;
		font-size: 11px;
		color: var(--color-text);
	}

	.token-continue {
		width: 100%;
		padding: 8px 12px;
	}
</style>

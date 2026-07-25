<script lang="ts">
	import { authStore } from '$lib/stores/auth';
	import { themeStore } from '$lib/stores/theme';
	import { goto } from '$app/navigation';

	function handleLogout(): void {
		authStore.logout();
		goto('/');
	}
</script>

<nav class="navbar">
	<div class="navbar-inner container">
		<a href="/" class="navbar-brand">
			<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
				<path d="M3 3h18v18H3z"/>
				<path d="M9 3v18"/>
				<path d="M3 9h6"/>
				<path d="M3 15h6"/>
			</svg>
			<span>git4cad</span>
		</a>

		<div class="navbar-actions">
			<button
				class="btn btn-sm theme-toggle"
				onclick={() => themeStore.toggle()}
				aria-label="Toggle theme"
				title={$themeStore === 'dark' ? 'Switch to light theme' : 'Switch to dark theme'}
			>
				{#if $themeStore === 'dark'}
					<!-- Sun icon -->
					<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<circle cx="12" cy="12" r="4"/>
						<path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/>
					</svg>
				{:else}
					<!-- Moon icon -->
					<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>
					</svg>
				{/if}
			</button>

			{#if $authStore.loggedIn}
				<a href="/{$authStore.username}" class="navbar-username">
					<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/>
						<circle cx="12" cy="7" r="4"/>
					</svg>
					{$authStore.username}
				</a>
				<button class="btn btn-sm" onclick={handleLogout}>
					Sign out
				</button>
			{:else}
				<a href="/login" class="btn btn-sm">Sign in</a>
			{/if}
		</div>
	</div>
</nav>

<style>
	.navbar {
		background-color: var(--color-navbar-bg);
		border-bottom: 1px solid var(--color-border);
		position: sticky;
		top: 0;
		z-index: 100;
	}

	.navbar-inner {
		display: flex;
		align-items: center;
		justify-content: space-between;
		height: 48px;
	}

	.navbar-brand {
		display: flex;
		align-items: center;
		gap: 8px;
		color: #e6edf3;
		font-size: 16px;
		font-weight: 700;
		text-decoration: none;
		letter-spacing: -0.02em;
	}

	.navbar-brand:hover {
		color: #2f81f7;
		text-decoration: none;
	}

	.navbar-brand svg {
		flex-shrink: 0;
		color: #2f81f7;
	}

	.navbar-actions {
		display: flex;
		align-items: center;
		gap: 12px;
	}

	.navbar-username {
		display: flex;
		align-items: center;
		gap: 6px;
		color: #e6edf3;
		font-size: 14px;
		font-weight: 500;
		text-decoration: none;
	}

	.navbar-username:hover {
		color: #2f81f7;
		text-decoration: none;
	}

	/* Navbar always has dark background — override btn colors for visibility */
	.theme-toggle,
	:global(.navbar .btn) {
		background-color: rgba(255, 255, 255, 0.08);
		border-color: rgba(255, 255, 255, 0.15);
		color: #e6edf3;
	}

	.theme-toggle:hover,
	:global(.navbar .btn:hover) {
		background-color: rgba(255, 255, 255, 0.15);
		border-color: rgba(255, 255, 255, 0.25);
	}
</style>

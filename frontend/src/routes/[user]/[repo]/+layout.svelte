<script lang="ts">
	import { page } from '$app/stores';
	import { onMount } from 'svelte';
	import { authStore } from '$lib/stores/auth';
	import { getRepo, type RepoInfo } from '$lib/api';

	let { children } = $props();

	let user = $derived($page.params.user);
	let repo = $derived($page.params.repo);

	let repoInfo = $state<RepoInfo | null>(null);
	let loading = $state(true);
	let error = $state('');
	let copied = $state(false);

	async function loadRepo() {
		loading = true;
		error = '';
		try {
			repoInfo = await getRepo(user, repo);
		} catch (e: any) {
			error = e.message || 'Repository not found';
		} finally {
			loading = false;
		}
	}

	async function copyCloneUrl() {
		if (!repoInfo?.clone_url) return;
		try {
			await navigator.clipboard.writeText(repoInfo.clone_url);
			copied = true;
			setTimeout(() => (copied = false), 2000);
		} catch {
			// ignore
		}
	}

	onMount(loadRepo);

	let currentPath = $derived($page.url.pathname);
	let isSettingsTab = $derived(currentPath.endsWith('/settings'));
	let isCommitsTab  = $derived(currentPath.endsWith('/commits'));
	let isCodeTab     = $derived(!isCommitsTab && !isSettingsTab);
	let isOwner       = $derived($authStore.username === user);
</script>

<svelte:head>
	<title>{user}/{repo} - git4cad</title>
</svelte:head>

<div class="repo-layout">
	{#if loading}
		<div class="container page loading-state">
			<div class="spinner"></div>
			<span>Loading repository...</span>
		</div>
	{:else if error}
		<div class="container page">
			<div class="alert alert-error">{error}</div>
		</div>
	{:else if repoInfo}
		<div class="repo-header">
			<div class="container">
				<div class="repo-title-row">
					<div class="repo-breadcrumb">
						<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
							<path d="M3 3h18v18H3z"/>
							<path d="M9 3v18"/>
						</svg>
						<a href="/{user}" class="bc-link">{user}</a>
						<span class="bc-sep">/</span>
						<a href="/{user}/{repo}" class="bc-link bc-repo">{repo}</a>
					</div>

					{#if repoInfo.private}
						<span class="badge-visibility">
							<svg width="11" height="11" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
								<path d="M18 8h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zM12 17c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2zm3.1-9H8.9V6c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2z"/>
							</svg>
							Private
						</span>
					{/if}
					{#if repoInfo.empty}
						<span class="badge-empty">Empty</span>
					{/if}
				</div>

				{#if repoInfo.description}
					<p class="repo-desc">{repoInfo.description}</p>
				{/if}

				{#if repoInfo.clone_url}
					<div class="clone-row">
						<div class="clone-input-wrap">
							<span class="clone-label">Clone</span>
							<input
								type="text"
								class="clone-url"
								value={repoInfo.clone_url}
								readonly
								onclick={(e) => (e.target as HTMLInputElement).select()}
							/>
							<button class="btn btn-sm copy-btn" onclick={copyCloneUrl} title="Copy URL">
								{#if copied}
									<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
										<polyline points="20 6 9 17 4 12"/>
									</svg>
									Copied
								{:else}
									<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
										<rect x="9" y="9" width="13" height="13" rx="2" ry="2"/>
										<path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>
									</svg>
									Copy
								{/if}
							</button>
						</div>
					</div>
				{/if}
			</div>
		</div>

		<div class="repo-tabs-bar">
			<div class="container">
				<nav class="tabs" aria-label="Repository navigation">
					<a
						href="/{user}/{repo}"
						class="tab"
						class:active={isCodeTab}
						aria-current={isCodeTab ? 'page' : undefined}
					>
						<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
							<polyline points="16 18 22 12 16 6"/>
							<polyline points="8 6 2 12 8 18"/>
						</svg>
						Code
					</a>
					<a
						href="/{user}/{repo}/commits"
						class="tab"
						class:active={isCommitsTab}
						aria-current={isCommitsTab ? 'page' : undefined}
					>
						<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
							<circle cx="12" cy="12" r="3"/>
							<line x1="3" y1="12" x2="9" y2="12"/>
							<line x1="15" y1="12" x2="21" y2="12"/>
						</svg>
						Commits
					</a>
					{#if isOwner}
						<a
							href="/{user}/{repo}/settings"
							class="tab"
							class:active={isSettingsTab}
							aria-current={isSettingsTab ? 'page' : undefined}
						>
							<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
								<circle cx="12" cy="12" r="3"/>
								<path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>
							</svg>
							Settings
						</a>
					{/if}
				</nav>
			</div>
		</div>

		<div class="repo-content container page">
			{@render children()}
		</div>
	{/if}
</div>

<style>
	.loading-state {
		display: flex;
		align-items: center;
		gap: 12px;
		color: var(--color-text-muted);
	}

	.repo-header {
		background-color: var(--color-surface);
		border-bottom: 1px solid var(--color-border);
		padding: 16px 0 0;
	}

	.repo-title-row {
		display: flex;
		align-items: center;
		gap: 10px;
		margin-bottom: 8px;
	}

	.repo-breadcrumb {
		display: flex;
		align-items: center;
		gap: 4px;
		font-size: 20px;
		font-weight: 600;
	}

	.repo-breadcrumb svg {
		color: var(--color-text-muted);
		flex-shrink: 0;
	}

	.bc-link {
		color: var(--color-accent);
		text-decoration: none;
		font-weight: 400;
	}

	.bc-link:hover {
		text-decoration: underline;
	}

	.bc-repo {
		font-weight: 600;
	}

	.bc-sep {
		color: var(--color-text-muted);
		font-weight: 300;
	}

	.badge-visibility {
		display: inline-flex;
		align-items: center;
		gap: 4px;
		font-size: 11px;
		font-weight: 500;
		padding: 2px 7px;
		border-radius: 20px;
		border: 1px solid var(--color-border);
		color: var(--color-text-muted);
	}

	.badge-empty {
		font-size: 11px;
		padding: 2px 6px;
		border-radius: 20px;
		border: 1px solid var(--color-border);
		color: var(--color-text-muted);
	}

	.repo-desc {
		font-size: 14px;
		color: var(--color-text-muted);
		margin-bottom: 12px;
	}

	.clone-row {
		margin-bottom: 16px;
	}

	.clone-input-wrap {
		display: flex;
		align-items: center;
		gap: 0;
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		overflow: hidden;
		max-width: 480px;
	}

	.clone-label {
		padding: 5px 10px;
		background-color: var(--color-surface-hover);
		border-right: 1px solid var(--color-border);
		font-size: 12px;
		font-weight: 600;
		color: var(--color-text-muted);
		white-space: nowrap;
	}

	.clone-url {
		flex: 1;
		padding: 5px 10px;
		background: transparent;
		border: none;
		border-radius: 0;
		font-family: var(--font-mono);
		font-size: 12px;
		color: var(--color-text);
		outline: none;
		min-width: 0;
	}

	.clone-url:focus {
		box-shadow: none;
	}

	.copy-btn {
		border-left: 1px solid var(--color-border);
		border-top: none;
		border-bottom: none;
		border-right: none;
		border-radius: 0;
		flex-shrink: 0;
	}

	.repo-tabs-bar {
		background-color: var(--color-surface);
		border-bottom: 1px solid var(--color-border);
	}

	.repo-tabs-bar .container {
		padding-bottom: 0;
	}

	.tabs {
		display: flex;
		margin-bottom: 0;
		border-bottom: none;
	}
</style>

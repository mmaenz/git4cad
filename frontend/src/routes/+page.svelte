<script lang="ts">
	import { onMount } from 'svelte';
	import { authStore } from '$lib/stores/auth';
	import { listRepos, createRepo, type RepoInfo } from '$lib/api';
	import RepoCard from '$lib/components/RepoCard.svelte';

	let repos = $state<RepoInfo[]>([]);
	let loading = $state(true);
	let error = $state('');

	// New repo modal state
	let showNewRepo = $state(false);
	let newRepoName = $state('');
	let newRepoDesc = $state('');
	let newRepoPrivate = $state(false);
	let creating = $state(false);
	let createError = $state('');

	async function loadRepos() {
		if (!$authStore.loggedIn) {
			loading = false;
			return;
		}
		try {
			loading = true;
			error = '';
			repos = await listRepos();
		} catch (e: any) {
			error = e.message || 'Failed to load repositories';
		} finally {
			loading = false;
		}
	}

	async function handleCreateRepo() {
		if (!newRepoName.trim()) return;
		creating = true;
		createError = '';
		try {
			await createRepo(newRepoName.trim(), newRepoDesc.trim() || undefined, newRepoPrivate);
			showNewRepo = false;
			newRepoName = '';
			newRepoDesc = '';
			newRepoPrivate = false;
			await loadRepos();
		} catch (e: any) {
			createError = e.message || 'Failed to create repository';
		} finally {
			creating = false;
		}
	}

	function handleKeydown(e: KeyboardEvent) {
		if (e.key === 'Escape') showNewRepo = false;
	}

	onMount(loadRepos);

	// Reload when auth state changes
	$effect(() => {
		if ($authStore.loggedIn) {
			loadRepos();
		} else {
			repos = [];
			loading = false;
		}
	});
</script>

<svelte:head>
	<title>git4cad - CAD version control</title>
</svelte:head>

{#if $authStore.loggedIn}
	<div class="container page">
		<div class="repos-header">
			<h2 class="section-title">Repositories</h2>
			<button class="btn btn-primary" onclick={() => (showNewRepo = true)}>
				<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<line x1="12" y1="5" x2="12" y2="19"/>
					<line x1="5" y1="12" x2="19" y2="12"/>
				</svg>
				New repository
			</button>
		</div>

		{#if error}
			<div class="alert alert-error">{error}</div>
		{/if}

		{#if loading}
			<div class="loading-state">
				<div class="spinner"></div>
				<span>Loading repositories...</span>
			</div>
		{:else if repos.length === 0}
			<div class="empty-state">
				<svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<path d="M3 3h18v18H3z"/>
					<path d="M9 3v18"/>
				</svg>
				<h3>No repositories yet</h3>
				<p>Create your first repository to start tracking CAD files.</p>
				<button class="btn btn-primary mt-3" onclick={() => (showNewRepo = true)}>
					Create a repository
				</button>
			</div>
		{:else}
			<div class="repo-grid">
				{#each repos as repo (repo.owner + '/' + repo.name)}
					<RepoCard {repo} />
				{/each}
			</div>
		{/if}
	</div>

	<!-- New Repo Modal -->
	{#if showNewRepo}
		<!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
		<div class="modal-backdrop" role="dialog" aria-modal="true" onkeydown={handleKeydown}>
			<div class="modal">
				<div class="modal-header">
					<h3>Create a new repository</h3>
					<button class="btn-icon" onclick={() => (showNewRepo = false)} aria-label="Close">
						<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
							<line x1="18" y1="6" x2="6" y2="18"/>
							<line x1="6" y1="6" x2="18" y2="18"/>
						</svg>
					</button>
				</div>

				{#if createError}
					<div class="alert alert-error">{createError}</div>
				{/if}

				<div class="form-group">
					<label for="repo-name">Repository name</label>
					<input
						id="repo-name"
						type="text"
						placeholder="my-cad-project"
						bind:value={newRepoName}
						autocomplete="off"
						spellcheck="false"
					/>
				</div>
				<div class="form-group">
					<label for="repo-desc">Description (optional)</label>
					<input
						id="repo-desc"
						type="text"
						placeholder="A short description..."
						bind:value={newRepoDesc}
					/>
				</div>

				<label class="visibility-toggle">
					<input type="checkbox" bind:checked={newRepoPrivate} />
					<span class="visibility-label">
						{#if newRepoPrivate}
							<svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
								<path d="M18 8h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zM12 17c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2zm3.1-9H8.9V6c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2z"/>
							</svg>
							<strong>Private</strong> — only you and collaborators can access
						{:else}
							<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
								<circle cx="12" cy="12" r="10"/>
								<line x1="2" y1="12" x2="22" y2="12"/>
								<path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>
							</svg>
							<strong>Public</strong> — anyone can view and clone
						{/if}
					</span>
				</label>

				<div class="modal-actions">
					<button class="btn" onclick={() => (showNewRepo = false)}>Cancel</button>
					<button
						class="btn btn-primary"
						onclick={handleCreateRepo}
						disabled={creating || !newRepoName.trim()}
					>
						{creating ? 'Creating...' : 'Create repository'}
					</button>
				</div>
			</div>
		</div>
	{/if}

{:else}
	<!-- Hero for unauthenticated users -->
	<div class="hero">
		<div class="hero-inner container-narrow">
			<div class="hero-logo">
				<svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<path d="M3 3h18v18H3z"/>
					<path d="M9 3v18"/>
					<path d="M3 9h6"/>
					<path d="M3 15h6"/>
				</svg>
			</div>
			<h1 class="hero-title">git4cad</h1>
			<p class="hero-subtitle">
				Version control for CAD files. Browse, diff, and collaborate on
				STEP, FreeCAD, and other 3D models — with an in-browser 3D viewer.
			</p>
			<div class="hero-actions">
				<a href="/login" class="btn btn-primary hero-btn">Sign in</a>
				<a href="/login?mode=register" class="btn hero-btn">Create an account</a>
			</div>
			<div class="hero-features">
				<div class="feature">
					<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<polyline points="16 18 22 12 16 6"/>
						<polyline points="8 6 2 12 8 18"/>
					</svg>
					<span>Git-based version control</span>
				</div>
				<div class="feature">
					<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/>
					</svg>
					<span>3D viewer for STEP and FCStd files</span>
				</div>
				<div class="feature">
					<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<circle cx="12" cy="12" r="10"/>
						<polyline points="12 6 12 12 16 14"/>
					</svg>
					<span>Full commit history</span>
				</div>
			</div>
		</div>
	</div>
{/if}

<style>
	.repos-header {
		display: flex;
		align-items: center;
		justify-content: space-between;
		margin-bottom: 16px;
	}

	.section-title {
		font-size: 20px;
		font-weight: 600;
	}

	.repo-grid {
		display: grid;
		grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
		gap: 16px;
	}

	.loading-state {
		display: flex;
		align-items: center;
		gap: 12px;
		padding: 32px;
		color: var(--color-text-muted);
	}

	.empty-state {
		display: flex;
		flex-direction: column;
		align-items: center;
		padding: 64px 16px;
		text-align: center;
		color: var(--color-text-muted);
		gap: 12px;
	}

	.empty-state h3 {
		font-size: 18px;
		color: var(--color-text);
	}

	.empty-state svg {
		opacity: 0.3;
	}

	/* Modal */
	.modal-backdrop {
		position: fixed;
		inset: 0;
		background-color: rgba(0, 0, 0, 0.7);
		display: flex;
		align-items: center;
		justify-content: center;
		z-index: 1000;
		padding: 16px;
	}

	.modal {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		padding: 24px;
		width: 100%;
		max-width: 480px;
	}

	.modal-header {
		display: flex;
		align-items: center;
		justify-content: space-between;
		margin-bottom: 20px;
	}

	.modal-header h3 {
		font-size: 16px;
		font-weight: 600;
	}

	.btn-icon {
		background: none;
		border: none;
		color: var(--color-text-muted);
		cursor: pointer;
		padding: 4px;
		display: flex;
		border-radius: var(--radius-sm);
	}

	.btn-icon:hover {
		color: var(--color-text);
		background-color: var(--color-surface-hover);
	}

	.visibility-toggle {
		display: flex;
		align-items: flex-start;
		gap: 10px;
		cursor: pointer;
		padding: 10px 12px;
		border: 1px solid var(--color-border);
		border-radius: var(--radius-sm);
		background-color: var(--color-bg);
		margin-bottom: 4px;
	}

	.visibility-toggle input[type='checkbox'] {
		margin-top: 2px;
		flex-shrink: 0;
		accent-color: var(--color-accent);
		width: 14px;
		height: 14px;
	}

	.visibility-label {
		display: flex;
		align-items: center;
		gap: 6px;
		font-size: 13px;
		color: var(--color-text);
		line-height: 1.4;
	}

	.visibility-label svg {
		flex-shrink: 0;
		color: var(--color-text-muted);
	}

	.modal-actions {
		display: flex;
		justify-content: flex-end;
		gap: 8px;
		margin-top: 24px;
	}

	/* Hero */
	.hero {
		display: flex;
		align-items: center;
		justify-content: center;
		min-height: calc(100vh - 48px);
		padding: 48px 16px;
	}

	.hero-inner {
		text-align: center;
	}

	.hero-logo {
		display: flex;
		justify-content: center;
		margin-bottom: 24px;
		color: var(--color-accent);
	}

	.hero-title {
		font-size: 48px;
		font-weight: 700;
		letter-spacing: -0.03em;
		margin-bottom: 16px;
		background: linear-gradient(135deg, var(--color-text) 0%, var(--color-accent) 100%);
		-webkit-background-clip: text;
		-webkit-text-fill-color: transparent;
		background-clip: text;
	}

	.hero-subtitle {
		font-size: 18px;
		color: var(--color-text-muted);
		line-height: 1.6;
		max-width: 520px;
		margin: 0 auto 32px;
	}

	.hero-actions {
		display: flex;
		gap: 12px;
		justify-content: center;
		margin-bottom: 48px;
	}

	.hero-btn {
		padding: 10px 24px;
		font-size: 15px;
	}

	.hero-features {
		display: flex;
		flex-direction: column;
		gap: 12px;
		align-items: flex-start;
		display: inline-flex;
	}

	.feature {
		display: flex;
		align-items: center;
		gap: 10px;
		font-size: 14px;
		color: var(--color-text-muted);
	}

	.feature svg {
		flex-shrink: 0;
		color: var(--color-success);
	}
</style>

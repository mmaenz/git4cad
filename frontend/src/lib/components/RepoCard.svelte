<script lang="ts">
	import type { RepoInfo } from '$lib/api';

	interface Props {
		repo: RepoInfo;
	}

	let { repo }: Props = $props();
</script>

<div class="repo-card">
	<div class="repo-header">
		<a href="/{repo.owner}/{repo.name}" class="repo-name">
			<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
				<path d="M3 3h18v18H3z"/>
				<path d="M9 3v18"/>
			</svg>
			<span class="owner">{repo.owner}</span>
			<span class="sep">/</span>
			<span class="name">{repo.name}</span>
		</a>
		{#if repo.private}
			<span class="badge badge-private">
				<svg width="10" height="10" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
					<path d="M18 8h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zM12 17c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2zm3.1-9H8.9V6c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2z"/>
				</svg>
				Private
			</span>
		{/if}
		{#if repo.empty}
			<span class="badge badge-empty">Empty</span>
		{/if}
	</div>

	{#if repo.description}
		<p class="repo-description">{repo.description}</p>
	{/if}

	<div class="repo-meta">
		<span class="meta-item">
			<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
				<circle cx="12" cy="12" r="3"/>
				<path d="M12 1v4M12 19v4M4.22 4.22l2.83 2.83M16.95 16.95l2.83 2.83M1 12h4M19 12h4M4.22 19.78l2.83-2.83M16.95 7.05l2.83-2.83"/>
			</svg>
			{repo.default_branch}
		</span>
	</div>
</div>

<style>
	.repo-card {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		padding: 16px;
		transition: border-color 0.12s ease;
	}

	.repo-card:hover {
		border-color: #8b949e;
	}

	.repo-header {
		display: flex;
		align-items: center;
		gap: 8px;
		margin-bottom: 8px;
	}

	.repo-name {
		display: flex;
		align-items: center;
		gap: 4px;
		font-size: 16px;
		font-weight: 600;
		text-decoration: none;
		color: var(--color-accent);
	}

	.repo-name:hover {
		text-decoration: underline;
	}

	.repo-name svg {
		flex-shrink: 0;
		color: var(--color-text-muted);
	}

	.owner {
		color: var(--color-text-muted);
		font-weight: 400;
	}

	.sep {
		color: var(--color-text-muted);
		font-weight: 400;
		margin: 0 1px;
	}

	.name {
		color: var(--color-accent);
	}

	.badge {
		font-size: 11px;
		font-weight: 500;
		padding: 2px 6px;
		border-radius: 20px;
		border: 1px solid;
	}

	.badge-empty {
		color: var(--color-text-muted);
		border-color: var(--color-border);
	}

	.badge-private {
		display: inline-flex;
		align-items: center;
		gap: 4px;
		color: var(--color-text-muted);
		border-color: var(--color-border);
	}

	.repo-description {
		color: var(--color-text-muted);
		font-size: 13px;
		margin-bottom: 12px;
		line-height: 1.5;
	}

	.repo-meta {
		display: flex;
		align-items: center;
		gap: 16px;
		flex-wrap: wrap;
	}

	.meta-item {
		display: flex;
		align-items: center;
		gap: 4px;
		font-size: 12px;
		color: var(--color-text-muted);
	}
</style>

<script lang="ts">
	import type { CommitInfo } from '$lib/api';
	import { relativeTime } from '$lib/api';

	interface Props {
		commits: CommitInfo[];
		user: string;
		repo: string;
	}

	let { commits, user, repo }: Props = $props();

	function shortSha(sha: string): string {
		return sha.slice(0, 7);
	}

	function firstLine(message: string): string {
		return message.split('\n')[0];
	}
</script>

<div class="commit-list">
	{#if commits.length === 0}
		<div class="empty">No commits yet.</div>
	{:else}
		{#each commits as commit (commit.sha)}
			<div class="commit-row">
				<div class="commit-info">
					<a
						href="/{user}/{repo}/commit/{commit.sha}"
						class="commit-message"
						title={commit.message}
					>
						{firstLine(commit.message)}
					</a>
					<div class="commit-meta">
						<span class="commit-author">{commit.author}</span>
						<span class="meta-sep">committed</span>
						<span class="commit-time" title={new Date(commit.timestamp * 1000).toISOString()}>
							{relativeTime(new Date(commit.timestamp * 1000).toISOString())}
						</span>
					</div>
				</div>
				<div class="commit-sha-cell">
					<a
						href="/{user}/{repo}/commit/{commit.sha}"
						class="commit-sha"
					>
						{shortSha(commit.sha)}
					</a>
				</div>
			</div>
		{/each}
	{/if}
</div>

<style>
	.commit-list {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		overflow: hidden;
	}

	.commit-row {
		display: flex;
		align-items: center;
		justify-content: space-between;
		padding: 12px 16px;
		border-bottom: 1px solid var(--color-border);
		gap: 16px;
	}

	.commit-row:last-child {
		border-bottom: none;
	}

	.commit-row:hover {
		background-color: var(--color-surface-hover);
	}

	.commit-info {
		flex: 1;
		min-width: 0;
	}

	.commit-message {
		display: block;
		font-size: 14px;
		font-weight: 500;
		color: var(--color-text);
		text-decoration: none;
		margin-bottom: 4px;
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}

	.commit-message:hover {
		color: var(--color-accent);
		text-decoration: underline;
	}

	.commit-meta {
		display: flex;
		align-items: center;
		gap: 4px;
		font-size: 12px;
		color: var(--color-text-muted);
		flex-wrap: wrap;
	}

	.commit-author {
		font-weight: 600;
		color: var(--color-text);
	}

	.meta-sep {
		color: var(--color-text-muted);
	}

	.commit-time {
		color: var(--color-text-muted);
	}

	.commit-sha-cell {
		flex-shrink: 0;
	}

	.commit-sha {
		font-family: var(--font-mono);
		font-size: 12px;
		color: var(--color-accent);
		text-decoration: none;
		padding: 2px 8px;
		background-color: rgba(47, 129, 247, 0.1);
		border: 1px solid rgba(47, 129, 247, 0.2);
		border-radius: var(--radius-sm);
		letter-spacing: 0.02em;
	}

	.commit-sha:hover {
		background-color: rgba(47, 129, 247, 0.2);
		text-decoration: none;
	}

	.empty {
		padding: 32px 16px;
		text-align: center;
		color: var(--color-text-muted);
		font-size: 14px;
	}
</style>

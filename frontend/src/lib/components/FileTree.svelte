<script lang="ts">
	import type { TreeEntry } from '$lib/api';
	import { formatBytes } from '$lib/api';

	interface Props {
		entries: TreeEntry[];
		user: string;
		repo: string;
		ref: string;
		currentPath: string;
	}

	let { entries, user, repo, ref, currentPath }: Props = $props();

	// Sort: folders first, then files, each alphabetically
	let sorted = $derived(
		[...entries].sort((a, b) => {
			if (a.type !== b.type) return a.type === 'tree' ? -1 : 1;
			return a.name.localeCompare(b.name);
		})
	);

	function entryHref(entry: TreeEntry): string {
		const fullPath = currentPath ? `${currentPath}/${entry.name}` : entry.name;
		if (entry.type === 'tree') {
			return `/${user}/${repo}/tree/${ref}/${fullPath}`;
		}
		return `/${user}/${repo}/blob/${ref}/${fullPath}`;
	}

	function parentHref(): string {
		const parts = currentPath.split('/').filter(Boolean);
		parts.pop();
		const parentPath = parts.join('/');
		return parentPath
			? `/${user}/${repo}/tree/${ref}/${parentPath}`
			: `/${user}/${repo}`;
	}
</script>

<div class="file-tree">
	<table>
		<thead>
			<tr>
				<th class="col-name">Name</th>
				<th class="col-size">Size</th>
			</tr>
		</thead>
		<tbody>
			{#if currentPath}
				<tr class="entry entry-parent">
					<td class="col-name">
						<a href={parentHref()} class="entry-link">
							<span class="icon icon-folder" aria-hidden="true">
								<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
									<path d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/>
								</svg>
							</span>
							<span class="entry-name">..</span>
						</a>
					</td>
					<td class="col-size"></td>
				</tr>
			{/if}

			{#each sorted as entry (entry.sha + entry.name)}
				<tr class="entry">
					<td class="col-name">
						<a href={entryHref(entry)} class="entry-link">
							{#if entry.type === 'tree'}
								<span class="icon icon-folder" aria-hidden="true">
									<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
										<path d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/>
									</svg>
								</span>
							{:else}
								<span class="icon icon-file" aria-hidden="true">
									<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
										<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
										<polyline points="14 2 14 8 20 8"/>
									</svg>
								</span>
							{/if}
							<span class="entry-name">{entry.name}</span>
						</a>
					</td>
					<td class="col-size">
						{#if entry.type === 'blob' && entry.size > 0}
							<span class="size-text">{formatBytes(entry.size)}</span>
						{/if}
					</td>
				</tr>
			{/each}
		</tbody>
	</table>
</div>

<style>
	.file-tree {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		overflow: hidden;
	}

	table {
		width: 100%;
		border-collapse: collapse;
	}

	thead {
		background-color: var(--color-surface-hover);
	}

	th {
		padding: 8px 16px;
		font-size: 12px;
		font-weight: 600;
		color: var(--color-text-muted);
		border-bottom: 1px solid var(--color-border);
		text-transform: none;
		letter-spacing: 0;
	}

	.col-name {
		width: 100%;
	}

	.col-size {
		min-width: 80px;
		text-align: right;
		white-space: nowrap;
	}

	.entry td {
		padding: 6px 16px;
		border-bottom: 1px solid var(--color-border);
		vertical-align: middle;
	}

	.entry:last-child td {
		border-bottom: none;
	}

	.entry:hover td {
		background-color: var(--color-surface-hover);
	}

	.entry-link {
		display: flex;
		align-items: center;
		gap: 8px;
		text-decoration: none;
		color: var(--color-text);
		font-size: 14px;
	}

	.entry-link:hover {
		color: var(--color-accent);
		text-decoration: none;
	}

	.entry-link:hover .entry-name {
		text-decoration: underline;
	}

	.icon {
		flex-shrink: 0;
		display: flex;
		align-items: center;
	}

	.icon-folder {
		color: #54aeff;
	}

	.icon-file {
		color: var(--color-text-muted);
	}

	.entry-name {
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}

	.size-text {
		font-size: 12px;
		color: var(--color-text-muted);
		font-family: var(--font-mono);
	}

	.entry-parent .entry-link {
		color: var(--color-text-muted);
	}
</style>
